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
#include "port_shape.h"
#include "debugmap.hpp"

namespace simgen {
using namespace detail;

namespace {
std::string cppStringLiteral(const std::string &s) {
    std::string out = "\"";
    for (char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    out += "\"";
    return out;
}

} // namespace

StaticTestHarnessCodeHpp genStaticTestHarnessCodeHpp(
    const VulStaticTestHarnessModule &test_module,
    const VulStaticModuleInstance &top_module,
    bool enable_tracing,
    const vector<VulBreakPointSpec> &break_specs,
    uint64_t break_cycles
) {

    vector<string> init_field;
    vector<string> member_field;
    VulDebugLocs member_field_debug;
    vector<string> public_member_field;
    vector<string> simulation_field;
    VulDebugLocs simulation_field_debug;
    vector<string> const_field;

    string child_instptr_name = "__instptr_top";
    string child_class_name = top_module.simClassName();

    member_field.push_back("std::unique_ptr<" + child_class_name + "> " + child_instptr_name + ";\n");
    init_field.push_back(CodeTab + child_instptr_name + " = std::make_unique<" + child_class_name + ">(this);\n");

    for (const auto &req_entry : test_module.requests) {
        const auto &req = req_entry.second;
        string rettype = req.returnType();
        string argnames = req.signatureArgNameList();
        string arglists = req.signatureArgOnly();
        const auto &port = top_module.services.at(req_entry.first);
        const bool is_arrayed = port.is_arrayed;
        const string callname = is_arrayed ? portFlatName(req_entry.first) : req_entry.first;
        if (is_arrayed) {
            member_field.push_back("template <uint32_t IDX>\n");
        }
        member_field.push_back(rettype + " " + callname + "(" + arglists + ") {\n");
        string return_prefix = (rettype == "void" ? "" : "return ");
        if (is_arrayed) {
            member_field.push_back(CodeTab + return_prefix + child_instptr_name + "->" + req_entry.first + "<IDX>(" + argnames + ");\n");
        } else {
            member_field.push_back(CodeTab + return_prefix + child_instptr_name + "->" + req_entry.first + "(" + argnames + ");\n");
        }
        member_field.push_back("}\n");
        member_field.push_back(portRequestFacade(port));
        member_field.push_back("\n");
    }

    for (const auto &query_entry : test_module.queries) {
        const string &query_name = query_entry.first;
        const auto &decl_query = query_entry.second;
        auto top_query_it = top_module.queries.find(query_name);
        if (top_query_it == top_module.queries.end()) {
            throw VulException("TestMain QUERY '" + query_name + "' not found in top module '" + top_module.module_name + "'");
        }
        const auto &top_query = top_query_it->second;
        if (!(decl_query.ret_type == top_query.ret_type)) {
            throw VulException("TestMain QUERY '" + query_name + "' return type mismatch with top module query");
        }
        member_field.push_back(top_query.ret_type.toString() + " " + query_name + "() const {\n");
        member_field.push_back(CodeTab + "return " + child_instptr_name + "->" + query_name + "();\n");
        member_field.push_back("}\n");
        member_field.push_back("\n");
    }

    for (const auto &serve : test_module.services) {
        const auto &serv_name = serve.first;
        const auto &serv = serve.second;
        string rettype = serv.returnType();
        string argnames = serv.signatureArgNameList();
        string arglists = serv.signatureArgOnly();
        const auto &resolved = top_module.requests.at(serv_name);
        const bool is_arrayed = resolved.is_arrayed;

        if (is_arrayed) {
            public_member_field.push_back("template <uint32_t IDX = 0>\n");
        }
        public_member_field.push_back(rettype + " __wrapper_" + top_module.instance_path.back() + "_" + serve.first + "(" + arglists + ") {\n");
        if (serv.has_handshake) {
            if (is_arrayed) {
                public_member_field.push_back(CodeTab + "bool cond = __cond_" + serve.first + "<IDX>(" + argnames + ");\n");
                public_member_field.push_back(CodeTab + "if (cond) __impl_" + serve.first + "<IDX>(" + argnames + ");\n");
            } else {
                public_member_field.push_back(CodeTab + "bool cond = __cond_" + serve.first + "(" + argnames + ");\n");
                public_member_field.push_back(CodeTab + "if (cond) __impl_" + serve.first + "(" + argnames + ");\n");
            }
            public_member_field.push_back(CodeTab + "return cond;\n");
        } else {
            if (is_arrayed) {
                public_member_field.push_back(CodeTab + "__impl_" + serve.first + "<IDX>(" + argnames + ");\n");
            } else {
                public_member_field.push_back(CodeTab + "__impl_" + serve.first + "(" + argnames + ");\n");
            }
        }
        public_member_field.push_back("}\n");

        if (is_arrayed) {
            member_field.push_back("template <uint32_t IDX = 0>\n");
        }
        member_field.push_back("void __impl_" + serv_name + "(" + arglists + ") {\n");
        vulDebugAppendLines(member_field, member_field_debug, lowerPortIndexDims(serv.codelines, resolved.port_dims, top_module.connection_config), serv.codelines_debug);
        member_field.push_back("}\n");
        if (serv.has_handshake) {
            if (is_arrayed) {
                member_field.push_back("template <uint32_t IDX = 0>\n");
            }
            member_field.push_back("bool __cond_" + serv_name + "(" + arglists + ") {\n");
            vulDebugAppendLine(member_field, member_field_debug, "return (" + lowerPortIndexDims(serv.cond, resolved.port_dims, top_module.connection_config) + ");\n", serv.cond_debug);
            member_field.push_back("}\n");
        }
    }

    simulation_field = test_module.test_codelines;
    simulation_field_debug = test_module.test_codelines_debug;

    vector<string> out_lines = genHeaderPrelude();
    VulDebugLocs out_debug;
    out_lines.push_back("#include \"common.h\"\n");
    out_lines.push_back("#include \"header.hpp\"\n");
    out_lines.push_back("#include \"vullib.h\"\n");
    out_lines.push_back("\n");

    out_lines.push_back("#include \"" + top_module.simDeclPath() + "\"\n");
    out_lines.push_back("\n");

    for (const auto &includes : test_module.includedHeaders) {
        out_lines.push_back("#include <" + includes + ">\n");
    }
    out_lines.push_back("\n");

    string class_name = top_module.parent->simClassName();
    out_lines.push_back("class " + class_name + " {\n");

    out_lines.push_back("protected:\n");
    for (const auto &line : const_field) {
        out_lines.push_back(line);
    }
    vulDebugAppendLines(out_lines, out_debug, test_module.globalCodes, test_module.globalCodes_debug);

    out_lines.push_back("public:\n");
    out_lines.push_back(class_name + "() {\n");
    for (const auto &line : init_field) {
        out_lines.push_back(line);
    }
    if (enable_tracing) {
        if (!break_specs.empty()) {
            out_lines.push_back(CodeTab + "trace_set_break_history_cycles(" + std::to_string(break_cycles) + ");\n");
            for (const auto &bp : break_specs) {
                std::string line = CodeTab + "trace_add_break_point({";
                for (size_t i = 0; i < bp.conditions.size(); ++i) {
                    if (i > 0) line += ", ";
                    const auto &cond = bp.conditions[i];
                    line += "{" + cppStringLiteral(cond.runtime_signal_name) + ", "
                                 + cppStringLiteral(cond.expected_bits) + ", "
                                 + cppStringLiteral(cond.expected_display) + "}";
                }
                line += "}, " + cppStringLiteral(bp.expr_text) + ");\n";
                out_lines.push_back(line);
            }
        }
        out_lines.push_back(CodeTab + "trace_init(\"trace.vcd\", 1, 1000);\n");
    }
    out_lines.push_back("}\n");
    out_lines.push_back("\n");
    out_lines.push_back("void simulation() {\n");
    vulDebugAppendLines(out_lines, out_debug, simulation_field, simulation_field_debug);
    out_lines.push_back("}\n");
    out_lines.push_back("\n");

    out_lines.insert(out_lines.end(), public_member_field.begin(), public_member_field.end());

    out_lines.push_back("protected:\n");
    out_lines.push_back("\n");

    out_lines.push_back("void sim_nextcycle() {\n");
    out_lines.push_back(CodeTab + "sim_execute();\n");
    out_lines.push_back(CodeTab + "sim_commit();\n");
    out_lines.push_back("}\n");
    out_lines.push_back("\n");

    out_lines.push_back("void sim_execute() {\n");
    out_lines.push_back(CodeTab + child_instptr_name + "->" + TickFunctionName + "();\n");
    out_lines.push_back("}\n");
    out_lines.push_back("\n");

    out_lines.push_back("void sim_commit() {\n");
    out_lines.push_back(CodeTab + child_instptr_name + "->" + ApplyTickFunctionName + "();\n");
    if (enable_tracing) {
        out_lines.push_back(CodeTab + "trace_commit();\n");
    }
    out_lines.push_back("}\n");
    out_lines.push_back("\n");

    out_lines.push_back("void sim_reset() {\n");
    out_lines.push_back(CodeTab + child_instptr_name + "->reset();\n");
    out_lines.push_back("}\n");
    out_lines.push_back("\n");

    vulDebugAppendLines(out_lines, out_debug, member_field, member_field_debug);

    out_lines.push_back("};\n");
    out_lines.push_back("\n");

    vulDebugNormalize(out_lines, out_debug);
    return {out_lines, out_debug, vulDebugBuildGeneratedMap(out_debug)};
}

vector<string> genStaticTestHarnessHpp(
    const VulStaticTestHarnessModule &test_module,
    const VulStaticModuleInstance &top_module,
    bool enable_tracing,
    const vector<VulBreakPointSpec> &break_specs,
    uint64_t break_cycles
) {
    return genStaticTestHarnessCodeHpp(
        test_module,
        top_module,
        enable_tracing,
        break_specs,
        break_cycles
    ).codes;
}

vector<string> genStaticTestMainHpp(shared_ptr<VulStaticModuleInstance> top_module) {

    vector<string> out_lines = genHeaderPrelude();

    out_lines.push_back("#include \"" + top_module->parent->simDeclPath() + "\"\n");
    out_lines.push_back("\n");

    // include all instances' impl headers
    std::set<string> included_impls;
    std::deque<shared_ptr<VulStaticModuleInstance>> bfs_queue;
    bfs_queue.push_back(top_module);
    while (!bfs_queue.empty()) {
        auto mod_ptr = bfs_queue.front();
        bfs_queue.pop_front();
        if (included_impls.insert(mod_ptr->simImplPath()).second)
            out_lines.push_back("#include \"" + mod_ptr->simImplPath() + "\"\n");
        for (const auto &child : mod_ptr->children) {
            bfs_queue.push_back(child);
        }
    }

    out_lines.push_back("\n");

    // alias top-sim module's class name to VulTestMain
    string top_class_name = top_module->parent->simClassName();
    out_lines.push_back("using VulTestMain = " + top_class_name + ";\n");
    out_lines.push_back("\n");

    return out_lines;
}




} // namespace simgen
