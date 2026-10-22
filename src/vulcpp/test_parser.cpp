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

#include "test_parser.hpp"

#include "common.hpp"
#include "declaration_args.hpp"
#include "test_handlers.hpp"
#include "source_files.hpp"
#include "../cppparse.hpp"
#include "../stringop.hpp"
#include "../vullib.hpp"

using namespace cppparse;
using namespace stringop;

VulStaticTestHarnessModule parseTestModule(
    const string &test_filepath,
    const VulStaticConfigLib &config_lib,
    const VulStaticBundleLib &bundle_lib
) {
    vulsource::requireExtension(test_filepath, vulsource::TestExtension, "Test Main");
    VCPPTestContext context;
    context.config_lib = config_lib;
    context.bundle_lib = bundle_lib;
    context.filepath = test_filepath;

    VulErrorContextGuard _err{"Parsing test module from file '" + test_filepath + "'"};

    vector<string> code_lines = readFileLines(test_filepath);
    auto trim_res = stripComments(code_lines);
    context.line_mapping = std::move(trim_res.mapping);
    code_lines = std::move(trim_res.lines);

    const string prefix = "#include";
    for (const auto &line_raw : code_lines) {
        string line = trim(line_raw);
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        size_t first_quote = line.find_first_of("\"<", prefix.size());
        if (first_quote == string::npos) {
            continue;
        }
        const bool angled = line[first_quote] == '<';
        size_t second_quote = line.find(angled ? '>' : '"', first_quote + 1);
        if (second_quote == string::npos || second_quote <= first_quote + 1) {
            continue;
        }
        string included_path = trim(line.substr(first_quote + 1, second_quote - first_quote - 1));
        if (vulsource::isVulInclude(included_path)) {
            continue;
        }
        // Quoted C++ headers retain the existing behavior (not propagated).
        if (!angled) {
            continue;
        }
        bool escaped = false;
        for (auto s : VulLibFiles) {
            if (included_path == s) {
                escaped = true;
                break;
            }
        }
        for (auto s : VulEscapedHeaders) {
            if (included_path == s) {
                escaped = true;
                break;
            }
        }
        if (escaped) {
            continue;
        }
        context.test.includedHeaders.push_back(included_path);
    }

    vector<MacroEntry> macro_entries = findAllMacroEntries(code_lines);
    for (const auto &entry : macro_entries) {
        VulErrorContextGuard declaration_context{"Declaration at " + context.getOriginalPosition(entry.pos)};
        VCPPTestHandlerRegistry::instance().run(context, normalizeDeclaration(entry, context.getOriginalPosition(entry.pos)));
    }

    return std::move(context.test);
}

std::pair<string, string> scanTestModuleBindingPaths(const string &test_filepath) {
    vulsource::requireExtension(test_filepath, vulsource::TestExtension, "Test Main");
    vector<string> code_lines = readFileLines(test_filepath);
    auto stripped = stripComments(code_lines);
    VCPPTestContext context;
    context.filepath = test_filepath;
    context.line_mapping = stripped.mapping;
    code_lines = std::move(stripped.lines);
    vector<MacroEntry> macro_entries = findAllMacroEntries(code_lines);

    string top_module_path;
    string project_dir_path;
    for (const auto &raw_entry : macro_entries) {
        if (raw_entry.name != "TOP" && raw_entry.name != "PROJECT") continue;
        const auto entry = normalizeDeclaration(raw_entry, test_filepath + ":" + context.getOriginalPosition(raw_entry.pos));
        if (entry.name == "TOP") {
            if (entry.args.size() != 1) {
                throw VulException("TOP requires exactly 1 argument");
            }
            if (!top_module_path.empty()) {
                throw VulException("Multiple TOP declarations are not allowed");
            }
            top_module_path = parsePathMacroArg(entry.args[0]);
        } else if (entry.name == "PROJECT") {
            if (entry.args.size() != 1) {
                throw VulException("PROJECT requires exactly 1 argument");
            }
            if (!project_dir_path.empty()) {
                throw VulException("Multiple PROJECT declarations are not allowed");
            }
            project_dir_path = parsePathMacroArg(entry.args[0]);
        }
    }
    return {top_module_path, project_dir_path};
}
