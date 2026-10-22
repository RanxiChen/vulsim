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

#include "module_parser.hpp"

#include "common.hpp"
#include "context.hpp"
#include "declaration_args.hpp"
#include "module_handlers.hpp"
#include "versioning.hpp"
#include "source_files.hpp"
#include "../cppparse.hpp"

#include <algorithm>

using namespace cppparse;

// PROJECT is file metadata, independent of INTERFACE and VERSION selection.
static string takeProjectPath(vector<MacroEntry> &entries, const VCPPModuleContext &context) {
    string result;
    bool seen = false;
    for (const auto &entry : entries) {
        if (entry.name != "PROJECT") continue;
        VulErrorContextGuard guard{"PROJECT at " + context.getOriginalPosition(entry.pos)};
        if (context.is_global_header) throw VulException("PROJECT is only allowed in module implementation files");
        if (seen) throw VulException("Multiple PROJECT declarations are not allowed");
        seen = true;
        if (entry.has_body) throw VulException("PROJECT must be a declaration without code block");
        result = parsePathMacroArg(entry.args.at(0));
        if (result.empty()) throw VulException("PROJECT path cannot be empty");
    }
    std::erase_if(entries, [](const auto &entry) { return entry.name == "PROJECT"; });
    return result;
}

string scanModuleProjectPath(const string &filepath) {
    vulsource::requireExtension(filepath, vulsource::ModuleExtension, "Module");
    VulErrorContextGuard guard{"scanning PROJECT in module file " + filepath};
    auto stripped = stripComments(readFileLines(filepath));
    VCPPModuleContext context;
    context.temp.filepath = filepath;
    context.line_mapping = std::move(stripped.mapping);
    auto entries = findAllMacroEntries(stripped.lines);
    // Only normalize metadata: global symbols are not available yet.
    for (auto &entry : entries) {
        if (entry.name == "PROJECT") entry = normalizeDeclaration(entry, context.getOriginalPosition(entry.pos));
    }
    return takeProjectPath(entries, context);
}

VulTempModule parseTempModule(
    const string &module_name,
    const string &module_filepath,
    unordered_map<string, string> *global_names,
    bool is_global_header
) {
    vulsource::requireExtension(module_filepath,
        is_global_header ? vulsource::HeaderExtension : vulsource::ModuleExtension,
        is_global_header ? "Global header" : "Module");
    VulErrorContextGuard _err{"Parsing module '" + module_name + "' from file '" + module_filepath + "'"};

    VCPPModuleContext context;
    context.temp.name = module_name;
    context.temp.filepath = module_filepath;
    context.global_names = global_names;
    context.is_global_header = is_global_header;

    vector<string> code_lines = readFileLines(module_filepath);
    auto trim_res = stripComments(code_lines);
    context.line_mapping = std::move(trim_res.mapping);
    code_lines = std::move(trim_res.lines);

    vector<MacroEntry> macro_entries = findAllMacroEntries(code_lines);
    for (auto &entry : macro_entries) {
        VulErrorContextGuard declaration_context{"Declaration at " + context.getOriginalPosition(entry.pos)};
        entry = normalizeDeclaration(entry, context.getOriginalPosition(entry.pos));
    }
    takeProjectPath(macro_entries, context);
    if (!is_global_header) {
        macro_entries = selectVersionedModuleEntries(macro_entries, context);
    }
    // printf("Found %zu macro entries in module '%s'\n", macro_entries.size(), module_name.c_str());
    for (const auto &entry : macro_entries) {
        // printf("Found macro: %s at %s: ", entry.name.c_str(), context.getOriginalPosition(entry.pos).c_str());
        // for (const auto &arg : entry.args) {
        //     printf("'%s' ", arg.c_str());
        // }
        // printf("\n");
        VCPPModuleHandlerRegistry::instance().run(context, entry);
    }

    return context.temp;
}

