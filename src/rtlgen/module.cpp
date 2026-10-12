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

#include "detail.hpp"
#include "common.hpp"
#include "instance_graph.h"
#include "apiinline/apiinline.hpp"
#include "debugmap.hpp"
#include <algorithm>
#include <cctype>

namespace rtlgen {
using namespace detail;
namespace {
struct HLSGlobalPortDecl {
    string direction;
    string name;
    string declaration;
};

HLSGlobalPortDecl globalizeHLSArgument(string argument) {
    HLSGlobalPortDecl out;
    size_t begin = argument.find_first_not_of(" \t");
    argument = begin == string::npos ? "" : argument.substr(begin);
    const bool is_input = argument.rfind("const ", 0) == 0;
    if (is_input) {
        argument.erase(0, 6);
    }
    argument.erase(
        std::remove(argument.begin(), argument.end(), '&'),
        argument.end());
    while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.back()))) {
        argument.pop_back();
    }
    size_t name_begin = argument.size();
    while (name_begin > 0) {
        const unsigned char ch = static_cast<unsigned char>(argument[name_begin - 1]);
        if (!(std::isalnum(ch) || ch == '_')) {
            break;
        }
        --name_begin;
    }
    out.direction = is_input ? "input_port" : "output_port";
    out.name = argument.substr(name_begin);
    out.declaration = argument + ";";
    return out;
}

static void insertHLSResetInitAfterFunctionOpen(
    vector<string> &lines,
    VulDebugLocs &debug,
    const vector<string> &reset_lines,
    const VulDebugLocs &reset_debug
) {
    if (reset_lines.empty()) {
        return;
    }
    string code = apiinline::joinLines(lines);
    size_t body_pos = apiinline::findLogicSubmoduleBodyInsertion(code);
    if (body_pos == string::npos) {
        return;
    }
    size_t insert_line = 0;
    size_t offset = 0;
    while (insert_line < lines.size() && offset + lines[insert_line].size() <= body_pos) {
        offset += lines[insert_line].size();
        ++insert_line;
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_line), reset_lines.begin(), reset_lines.end());
    debug.insert(debug.begin() + static_cast<std::ptrdiff_t>(insert_line), reset_debug.begin(), reset_debug.end());
}

static void replaceHLSResetInitAnchor(
    vector<string> &lines,
    VulDebugLocs &debug,
    const vector<string> &reset_lines,
    const VulDebugLocs &reset_debug
) {
    if (reset_lines.empty()) {
        return;
    }
    constexpr const char *anchor = "const bool __vul_register_reset_insert_anchor = false;\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == anchor) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            debug.erase(debug.begin() + static_cast<std::ptrdiff_t>(i));
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), reset_lines.begin(), reset_lines.end());
            debug.insert(debug.begin() + static_cast<std::ptrdiff_t>(i), reset_debug.begin(), reset_debug.end());
            return;
        }
    }
    insertHLSResetInitAfterFunctionOpen(lines, debug, reset_lines, reset_debug);
}

} // namespace

RTLGenResult genModuleRTL(
    const VulStaticModuleInstance &original,
    const VulStaticConfigLib &configlib,
    const VulStaticBundleLib &bundlelib,
    const vector<string> &global_helper_codes
) {
    const auto module = concreteGenerationView(original);
    VulStaticConfigLib local_configlib = configlib;
    for (const auto &entry : module.local_parameters) {
        local_configlib[entry.first] = entry.second;
    }
    for (const auto &entry : module.local_consts) {
        local_configlib[entry.first] = entry.second;
    }

    VulStaticBundleLib local_bundlelib = bundlelib;
    for (const auto &local_bundle : module.local_bundles) {
        bool replaced = false;
        for (auto &bundle : local_bundlelib) {
            if (bundle.name == local_bundle.name) {
                bundle = local_bundle;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            local_bundlelib.push_back(local_bundle);
        }
    }

    RTLGenContext ctx(module, local_configlib, local_bundlelib, global_helper_codes);

    _procConstAndBundle(ctx);
    _procWires(ctx);
    _procRequests(ctx);
    _procQueries(ctx);
    _procServicesAndTicks(ctx);
    _procChildrenAndConnection(ctx);
    _procQueues(ctx);
    _procBRAMAndROM(ctx);
    vector<string> usage_lines = ctx.hls_blocks;
    usage_lines.insert(usage_lines.end(), ctx.hls_body.begin(), ctx.hls_body.end());
    usage_lines.insert(usage_lines.end(), ctx.hls_final.begin(), ctx.hls_final.end());
    ctx.register_api_usage = apiinline::checkRegisterAPIUsage(module, usage_lines);
    _procRegisters(ctx);

    RTLGenResult result;
    result.has_logic_submodule = !ctx.hls_arguments.empty();
    for (const auto &connection : ctx.rtl_logicports) {
        const size_t open = connection.find('(');
        if (connection.empty() || connection.front() != '.' || open == string::npos ||
            connection.back() != ')' || open <= 1) {
            throw VulException("Malformed RTL logic port connection: " + connection);
        }
        result.logic_port_bindings.emplace_back(
            connection.substr(1, open - 1),
            connection.substr(open + 1, connection.size() - open - 2));
    }

    const string module_name = module.rtlConcreteClassName();
    const string logic_module_name = LogicSubModuleName(module_name);

    auto append_lines = [](vector<string> &dst, VulDebugLocs &dst_debug, vector<string> &src, VulDebugLocs &src_debug) {
        vulDebugNormalize(src, src_debug);
        for (size_t i = 0; i < src.size(); ++i) {
            string line = src[i];
            if (line.empty() || line.back() != '\n') {
                line.push_back('\n');
            }
            vulDebugAppendLine(dst, dst_debug, line, src_debug[i]);
        }
    };

    auto &hls = result.logic_hls_codes;
    auto &hls_debug = result.logic_hls_debug;
    if (result.has_logic_submodule) {
        hls.push_back("#include <array>\n");
        hls.push_back("#include <cstdint>\n");
        hls.push_back("#include <type_traits>\n");
        hls.push_back("#include <fixint.hpp>\n");
        hls.push_back("\n");
        hls.push_back("using std::array;\n");
        hls.push_back("\n");
        hls.push_back("using int8 = int8_t;\n");
        hls.push_back("using uint8 = uint8_t;\n");
        hls.push_back("using int16 = int16_t;\n");
        hls.push_back("using uint16 = uint16_t;\n");
        hls.push_back("using int32 = int32_t;\n");
        hls.push_back("using uint32 = uint32_t;\n");
        hls.push_back("using int64 = int64_t;\n");
        hls.push_back("using uint64 = uint64_t;\n");
        hls.push_back("\n");
        append_lines(hls, hls_debug, ctx.hls_header, ctx.hls_header_debug);
        if (!ctx.hls_header.empty()) {
            hls.push_back("\n");
        }

        for (const auto &argument : ctx.hls_arguments) {
            const HLSGlobalPortDecl port = globalizeHLSArgument(argument);
            hls.push_back("#pragma " + port.direction + " " + port.name + "\n");
            hls.push_back(port.declaration + "\n");
        }
        hls.push_back("\n");

        hls.push_back("void " + logic_module_name + "() {\n");

        append_lines(hls, hls_debug, ctx.hls_init, ctx.hls_init_debug);
        if (!ctx.hls_init.empty()) {
            hls.push_back("\n");
        }
        append_lines(hls, hls_debug, ctx.hls_helpers, ctx.hls_helpers_debug);
        if (!ctx.hls_helpers.empty()) {
            hls.push_back("\n");
        }
        if (!ctx.hls_reset_init.empty()) {
            hls.push_back("const bool __vul_register_reset_insert_anchor = false;\n");
            hls_debug.push_back({});
            hls.push_back("\n");
            hls_debug.push_back({});
        }
        append_lines(hls, hls_debug, ctx.hls_blocks, ctx.hls_blocks_debug);
        if (!ctx.hls_blocks.empty()) {
            hls.push_back("\n");
        }
        append_lines(hls, hls_debug, ctx.hls_body, ctx.hls_body_debug);
        if (!ctx.hls_body.empty()) {
            hls.push_back("\n");
        }
        append_lines(hls, hls_debug, ctx.hls_final, ctx.hls_final_debug);
        hls.push_back("}\n");
        hls.push_back("\n");
        vulDebugNormalize(result.logic_hls_codes, result.logic_hls_debug);
    }

    auto &rtl = result.rtl_skeleten_codes;
    vector<string> ports;
    ports.reserve(2 + ctx.rtl_ports.size());
    ports.push_back("input clk");
    ports.push_back("input rstn");
    ports.insert(ports.end(), ctx.rtl_ports.begin(), ctx.rtl_ports.end());

    rtl.push_back("`timescale 1ns/1ps\n");
    rtl.push_back("\n");
    rtl.push_back("module " + module_name + "(\n");
    for (size_t i = 0; i < ports.size(); ++i) {
        rtl.push_back("  " + ports[i] + (i + 1 == ports.size() ? "" : ",") + "\n");
    }
    rtl.push_back(");\n");
    rtl.push_back("\n");

    rtl.insert(rtl.end(), ctx.rtl_decl.begin(), ctx.rtl_decl.end());
    if (!ctx.rtl_decl.empty()) {
        rtl.push_back("\n");
    }
    rtl.insert(rtl.end(), ctx.rtl_logic.begin(), ctx.rtl_logic.end());
    if (!ctx.rtl_logic.empty()) {
        rtl.push_back("\n");
    }

    rtl.insert(rtl.end(), ctx.rtl_inst.begin(), ctx.rtl_inst.end());
    if (!ctx.rtl_inst.empty()) {
        rtl.push_back("\n");
    }
    rtl.push_back("endmodule\n");
    rtl.push_back("\n");
    vulDebugNormalize(result.rtl_skeleten_codes, result.rtl_skeleten_debug);

    result.resource_files = std::move(ctx.resource_files);
    if (result.has_logic_submodule) {
        auto inlined = apiinline::inlineAPIs(module, local_bundlelib, result.logic_hls_codes, result.logic_hls_debug);
        result.logic_hls_codes = std::move(inlined.lines);
        result.logic_hls_debug = std::move(inlined.debug);
        replaceHLSResetInitAnchor(result.logic_hls_codes, result.logic_hls_debug, ctx.hls_reset_init, ctx.hls_reset_init_debug);
        vulDebugNormalize(result.logic_hls_codes, result.logic_hls_debug);
    }
    result.logic_hls_debug_lines = vulDebugBuildGeneratedMap(result.logic_hls_debug);
    result.rtl_skeleten_debug_lines = vulDebugBuildGeneratedMap(result.rtl_skeleten_debug);

    return result;
}


} // namespace rtlgen
