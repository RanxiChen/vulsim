// Copyright (c) 2025 Meng Chengzhen, in Shandong University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rtlgen.h"

#include "debugmap.hpp"
#include "rtlzz_bridge.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace rtlgen {

namespace {

string resetLiteral(int width, const vector<std::uint64_t>& words, int element) {
    const std::size_t stride = (static_cast<std::size_t>(width)+63)/64;
    if (width <= 0 || (element+1)*stride > words.size()) throw VulException("Invalid pipeline output reset shape");
    std::ostringstream hex;
    hex << std::hex << words[element*stride+stride-1];
    for (std::size_t limb = stride-1; limb>0; --limb) hex << std::setw(16) << std::setfill('0') << words[element*stride+limb-1];
    return std::to_string(width) + "'h" + hex.str();
}

void appendTextAsLines(vector<string> &lines, const string &text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t next = text.find('\n', pos);
        if (next == string::npos) {
            lines.push_back(text.substr(pos) + "\n");
            return;
        }
        lines.push_back(text.substr(pos, next - pos + 1));
        pos = next + 1;
    }
}

} // namespace

LogicRTLResult appendLogicRTL(
    RTLGenResult &result,
    const VulStaticModuleInstance &module,
    const string &logic_hls_filepath,
    const string &lib_include_dir,
    int unroll_limit,
    bool release,
    bool use_circt,
    std::function<void(const std::string &)> progress_callback
) {
    vector<string> pipeline_debug;
    std::map<string, vector<std::uint64_t>> output_reset_values;
    for (const auto &unit : result.pipelines) {
        const string path = logic_hls_filepath + ".pipeline_" + unit.name + ".cpp";
        struct Cleanup { string path; bool enabled; ~Cleanup() { if (enabled) { std::error_code ec; std::filesystem::remove(path, ec); } } } cleanup{path, release};
        { std::ofstream file(path); if (!file) throw VulException("Cannot write pipeline source: " + path); for (const auto &line : unit.source) file << line; }
        const string reset_path = path + ".reset.cpp";
        Cleanup reset_cleanup{reset_path, release};
        { std::ofstream file(reset_path); if (!file) throw VulException("Cannot write reset source: " + reset_path); for (const auto &line : unit.reset_source) file << line; }
        auto reset_inputs = generateLogicRTLWithRTLzz(reset_path, unit.reset_top, lib_include_dir, {}, unroll_limit, release, false, progress_callback, 0, true, {}, true);
        if (!reset_inputs.ok) {
            LogicRTLResult error;
            error.ok = false;
            error.error = "Input reset values for pipeline '" + unit.name + "': " + reset_inputs.error;
            error.error_debug_codelines = std::move(reset_inputs.error_debug_codelines);
            return error;
        }
        if (!release) vulDebugWriteMapToFile(vulDebugBuildGeneratedMap(unit.debug), path + ".dbgmap");
        const string step = "pipeline " + unit.name + ": latency=" + std::to_string(unit.latency) + (use_circt ? ", CIRCT" : ", native RTLZZ");
        if (progress_callback) progress_callback(step);
        else std::cerr << "[vulrtlgen] module " << module.simClassName() << ": " << step << "\n";
        auto compiled = generateLogicRTLWithRTLzz(path, unit.top_function, lib_include_dir, unit.bindings, unroll_limit, release, use_circt, progress_callback, unit.latency - 1, true, reset_inputs.output_reset_values, false, unit.input_valid_name, unit.output_valid_name);
        if (!compiled.ok) {
            LogicRTLResult error;
            error.ok = false;
            error.error = "Pipeline '" + unit.name + "' in " + module.simClassName() + ": " + compiled.error;
            error.error += "\nPipeline source: " + unit.declaration_loc.file + ":" + std::to_string(unit.declaration_loc.line);
            error.error_debug_codelines = std::move(compiled.error_debug_codelines);
            error.error_signal_debug_text = std::move(compiled.error_signal_debug_text);
            error.error_signal_names = std::move(compiled.error_signal_names);
            return error;
        }
        for (const auto &[name, shape] : unit.output_shapes) {
            auto values = compiled.output_reset_values.find("__vul_port_" + name);
            const auto [width, count] = shape;
            if (values == compiled.output_reset_values.end() || values->second.size() != ((width+63)/64)*count) throw VulException("Missing output reset value: " + name);
            output_reset_values[name] = values->second;
            string report = "oreg_reset " + name + " vector<uint64_t>=[";
            for (std::size_t i=0; i<values->second.size(); ++i) report += (i ? "," : "") + std::to_string(values->second[i]);
            pipeline_debug.push_back(report + "]\n");
            for (int i=0; i<count; ++i) {
                const string placeholder = "__vul_pipeline_reset_" + name + "_" + std::to_string(i);
                const string literal = resetLiteral(width, values->second, i);
                for (auto &line : result.rtl_skeleten_codes) line = identifierReplace(line, placeholder, literal);
            }
        }
        auto end = std::find(result.rtl_skeleten_codes.begin(), result.rtl_skeleten_codes.end(), "endmodule\n");
        if (end == result.rtl_skeleten_codes.end()) throw VulException("Missing RTL endmodule");
        const size_t offset = end - result.rtl_skeleten_codes.begin();
        vector<string> lines{"if (1) begin : __vul_pipeline_" + unit.name + "\n"};
        appendTextAsLines(lines, compiled.rtl_text);
        lines.push_back("end\n");
        result.rtl_skeleten_codes.insert(end, lines.begin(), lines.end());
        result.rtl_skeleten_debug.insert(result.rtl_skeleten_debug.begin() + offset, lines.size(), {});
        for (auto &line : compiled.debug_codelines) {
            const string prefix = "  rtl_line: ";
            if (line.rfind(prefix, 0) != 0) continue;
            const auto begin = prefix.size();
            const auto end = line.find_first_not_of("0123456789", begin);
            if (end == begin) continue;
            auto relative = std::stoul(line.substr(begin, end - begin));
            if (relative) line.replace(begin, end - begin, std::to_string(relative + offset + 1));
        }
        pipeline_debug.push_back("pipeline: " + unit.name + " latency=" + std::to_string(unit.latency) + " internal_stages=" + std::to_string(unit.latency - 1) + "\n");
        pipeline_debug.insert(pipeline_debug.end(), compiled.debug_codelines.begin(), compiled.debug_codelines.end());
    }
    vulDebugNormalize(result.rtl_skeleten_codes, result.rtl_skeleten_debug);
    result.rtl_skeleten_debug_lines = vulDebugBuildGeneratedMap(result.rtl_skeleten_debug);
    if (!result.has_logic_submodule || result.logic_hls_codes.empty()) {
        LogicRTLResult out; out.debug_codelines = std::move(pipeline_debug); out.output_reset_values = std::move(output_reset_values); return out;
    }

    VulErrorContextGuard rtlzz_err("running RTLzz for logic submodule: " + module.simClassName());
    const auto logic_module_name = LogicSubModuleName(module.rtlConcreteClassName());
    RTLzzLogicRTLResult logic_rtl = generateLogicRTLWithRTLzz(
        logic_hls_filepath,
        logic_module_name,
        lib_include_dir,
        result.logic_port_bindings,
        unroll_limit,
        release,
        use_circt,
        std::move(progress_callback)
    );
    if (!logic_rtl.ok) {
        LogicRTLResult out;
        out.ok = false;
        out.error = "RTLzz compile failed for '" + logic_module_name + "' in '" + logic_hls_filepath + "': " + logic_rtl.error;
        out.error_debug_codelines = std::move(logic_rtl.error_debug_codelines);
        out.error_signal_debug_text = std::move(logic_rtl.error_signal_debug_text);
        out.error_signal_names = std::move(logic_rtl.error_signal_names);
        return out;
    }

    auto endmodule = std::find(result.rtl_skeleten_codes.begin(),
                               result.rtl_skeleten_codes.end(), "endmodule\n");
    if (endmodule == result.rtl_skeleten_codes.end()) {
        LogicRTLResult out;
        out.ok = false;
        out.error = "RTL module framework is missing endmodule";
        return out;
    }
    vector<string> body_lines;
    body_lines.push_back("\n");
    appendTextAsLines(body_lines, logic_rtl.rtl_text);
    const auto offset = static_cast<size_t>(endmodule - result.rtl_skeleten_codes.begin());
    result.rtl_skeleten_codes.insert(endmodule, body_lines.begin(), body_lines.end());
    result.rtl_skeleten_debug.insert(result.rtl_skeleten_debug.begin() + offset,
                                     body_lines.size(), {});
    vulDebugNormalize(result.rtl_skeleten_codes, result.rtl_skeleten_debug);
    result.rtl_skeleten_debug_lines = vulDebugBuildGeneratedMap(result.rtl_skeleten_debug);
    const string line_prefix = "  rtl_line: ";
    for (auto &line : logic_rtl.debug_codelines) {
        if (line.rfind(line_prefix, 0) != 0) continue;
        const size_t number_begin = line_prefix.size();
        const size_t number_end = line.find_first_not_of("0123456789", number_begin);
        if (number_end == number_begin) continue;
        const auto relative_line = std::stoul(line.substr(number_begin, number_end - number_begin));
        if (relative_line == 0) continue;
        line.replace(number_begin, number_end - number_begin,
                     std::to_string(relative_line + offset + 1));
    }
    LogicRTLResult out;
    out.output_reset_values = std::move(output_reset_values);
    out.debug_codelines = std::move(pipeline_debug);
    out.debug_codelines.insert(out.debug_codelines.end(), logic_rtl.debug_codelines.begin(), logic_rtl.debug_codelines.end());
    return out;
}

} // namespace rtlgen
