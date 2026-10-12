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
namespace detail {

namespace {
string moduleWrapperBaseName(const VulStaticModuleInstance &mod) { return mod.instance_path.back(); }

} // namespace

void emitTransactions(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    const auto &mod_class_name = ctx.mod_class_name;
    auto &decl_public_field = ctx.decl_public_field;
    auto &decl_private_field = ctx.decl_private_field;
    auto &impl_field = ctx.impl_field;
    auto &impl_field_debug = ctx.impl_field_debug;
    auto &impl_commit_field = ctx.impl_commit_field;
    auto &impl_sys_reset_field = ctx.impl_sys_reset_field;
    // generate request declarations
    for (const auto &req_entry : mod.requests) {
        const auto &req = req_entry.second;
        string rettype = req.returnType();
        string argnames = req.signatureArgNameList();
        string arglists = req.signatureArgOnly();
        const bool is_arrayed = req.is_arrayed;
        const string callname = is_arrayed ? portFlatName(req_entry.first) : req_entry.first;

        if (is_arrayed) {
            decl_private_field.push_back("template <uint32_t IDX>\n");
            decl_private_field.push_back(rettype + " " + callname + "(" + arglists + ");\n");
        } else {
            decl_private_field.push_back(rettype + " " + callname + "(" + arglists + ");\n");
        }
        decl_private_field.push_back(portRequestFacade(req));
        decl_private_field.push_back("\n");

        string call_prefix = (rettype == "void" ? "" : "return ");
        string wrapper_name = "__parent->__wrapper_" + moduleWrapperBaseName(mod) + "_" + req_entry.first;
        string wrapper_prefix_args;
        wrapper_prefix_args += argnames;
        if (is_arrayed) {
            impl_field.push_back("template <uint32_t IDX>\n");
            impl_field.push_back(rettype + " " + mod_class_name + "::" + callname + "(" + arglists + ") {\n");
            impl_field.push_back(CodeTab + call_prefix + wrapper_name + "<IDX>(" + wrapper_prefix_args + ");\n");
        } else {
            impl_field.push_back(rettype + " " + mod_class_name + "::" + callname + "(" + arglists + ") {\n");
            impl_field.push_back(CodeTab + call_prefix + wrapper_name + "(" + wrapper_prefix_args + ");\n");
        }
        impl_field.push_back("}\n");
        impl_field.push_back("\n");
    }

    // generate service declarations
    for (const auto &serv_entry : mod.services) {
        const auto &serv = serv_entry.second;
        const string call_guard_name = "__service_called_" + serv_entry.first;
        const string service_assert_msg =
            "SERVICE '" + mod.module_name + "::" + serv_entry.first +
            "' may only be called once per cycle";
        string rettype = serv.returnType();
        string argnames = serv.signatureArgNameList();
        string arglists = serv.signatureArgOnly();
        const bool is_arrayed = serv.is_arrayed;

        if (is_arrayed) {
            decl_private_field.push_back("std::array<bool, " + std::to_string(serv.array_size) + "> " + call_guard_name + ";\n");
            impl_sys_reset_field.push_back("for (auto &__flag : " + call_guard_name + ") __flag = false;\n");
            impl_commit_field.push_back("for (auto &__flag : " + call_guard_name + ") __flag = false;\n");
        } else {
            decl_private_field.push_back("bool " + call_guard_name + ";\n");
            impl_sys_reset_field.push_back(call_guard_name + " = false;\n");
            impl_commit_field.push_back(call_guard_name + " = false;\n");
        }

        if (is_arrayed) {
            decl_public_field.push_back("template <uint32_t IDX = 0>\n");
            decl_public_field.push_back(rettype + " " + serv_entry.first + "(" + arglists + ");\n");
        } else {
            decl_public_field.push_back(rettype + " " + serv_entry.first + "(" + arglists + ");\n");
        }

        if (serv.port_dims.size() > 1) {
            string facade = portRequestFacade(serv);
            facade.erase(facade.find("}\n") + 2);
            const string flat = portFlatName(serv.name);
            size_t pos = facade.find(flat);
            if (pos != string::npos) facade.replace(pos, flat.size(), serv.name);
            decl_public_field.push_back(facade);
        }

        // implemented by logic block, or connented to child module's service
        auto lb_iter = mod.serv_logic_blocks.find(serv_entry.first);
        if (lb_iter != mod.serv_logic_blocks.end()) {
            if (rettype == "void") {
                if (is_arrayed) {
                    impl_field.push_back("template <uint32_t IDX>\n");
                }
                impl_field.push_back("void " + mod_class_name + "::" + serv_entry.first + "(" + arglists + ") {\n");
                if (is_arrayed) {
                    impl_field.push_back(CodeTab + "assert(!" + call_guard_name + "[IDX] && \"" + service_assert_msg + " (for each array index)\");\n");
                    impl_field.push_back(CodeTab + call_guard_name + "[IDX] = true;\n");
                } else {
                    impl_field.push_back(CodeTab + "assert(!" + call_guard_name + " && \"" + service_assert_msg + "\");\n");
                    impl_field.push_back(CodeTab + call_guard_name + " = true;\n");
                }
                vulDebugAppendLines(impl_field, impl_field_debug, lowerPortIndexDims(lb_iter->second.codelines, serv.port_dims, mod.connection_config), lb_iter->second.codelines_debug);
                impl_field.push_back("}\n");
            } else {
                if (is_arrayed) {
                    decl_private_field.push_back("template <uint32_t IDX = 0>\n");
                    decl_private_field.push_back("bool __cond_" + serv_entry.first + "(" + arglists + ");\n");
                    decl_private_field.push_back("template <uint32_t IDX = 0>\n");
                    decl_private_field.push_back("void __impl_" + serv_entry.first + "(" + arglists + ");\n");
                } else {
                    decl_private_field.push_back("bool __cond_" + serv_entry.first + "(" + arglists + ");\n");
                    decl_private_field.push_back("void __impl_" + serv_entry.first + "(" + arglists + ");\n");
                }

                if (is_arrayed) {
                    impl_field.push_back("template <uint32_t IDX>\n");
                }
                impl_field.push_back("bool " + mod_class_name + "::" + serv_entry.first + "(" + arglists + ") {\n");
                if (is_arrayed) {
                    impl_field.push_back(CodeTab + "assert(!" + call_guard_name + "[IDX] && \"" + service_assert_msg + " (for each array index)\");\n");
                    impl_field.push_back(CodeTab + call_guard_name + "[IDX] = true;\n");
                } else {
                    impl_field.push_back(CodeTab + "assert(!" + call_guard_name + " && \"" + service_assert_msg + "\");\n");
                    impl_field.push_back(CodeTab + call_guard_name + " = true;\n");
                }
                if (is_arrayed) {
                    impl_field.push_back(CodeTab + "bool cond = __cond_" + serv_entry.first + "<IDX>(" + argnames + ");\n");
                    impl_field.push_back(CodeTab + "if (cond) __impl_" + serv_entry.first + "<IDX>(" + argnames + ");\n");
                } else {
                    impl_field.push_back(CodeTab + "bool cond = __cond_" + serv_entry.first + "(" + argnames + ");\n");
                    impl_field.push_back(CodeTab + "if (cond) __impl_" + serv_entry.first + "(" + argnames + ");\n");
                }
                impl_field.push_back(CodeTab + "return cond;\n");
                impl_field.push_back("}\n");

                if (is_arrayed) {
                    impl_field.push_back("template <uint32_t IDX>\n");
                }
                impl_field.push_back("bool " + mod_class_name + "::__cond_" + serv_entry.first + "(" + arglists + ") {\n");
                vulDebugAppendLines(impl_field, impl_field_debug, lowerPortIndexDims(lb_iter->second.cond_codelines, serv.port_dims, mod.connection_config), lb_iter->second.cond_codelines_debug);
                impl_field.push_back("}\n");

                if (is_arrayed) {
                    impl_field.push_back("template <uint32_t IDX>\n");
                }
                impl_field.push_back("void " + mod_class_name + "::__impl_" + serv_entry.first + "(" + arglists + ") {\n");
                vulDebugAppendLines(impl_field, impl_field_debug, lowerPortIndexDims(lb_iter->second.codelines, serv.port_dims, mod.connection_config), lb_iter->second.codelines_debug);
                impl_field.push_back("}\n");
            }
        }

        if (lb_iter == mod.serv_logic_blocks.end()) {
            if (is_arrayed) impl_field.push_back("template <uint32_t IDX>\n");
            impl_field.push_back(rettype + " " + mod_class_name + "::" + serv_entry.first + "(" + arglists + ") {\n");
            const string flag = call_guard_name + (is_arrayed ? "[IDX]" : "");
            impl_field.push_back(CodeTab + "assert(!" + flag + " && \"" + service_assert_msg + "\");\n");
            impl_field.push_back(CodeTab + flag + " = true;\n");
            bool found = false;
            for (const auto &forward : mod.req_connections) {
                if (!forward.req_instance.empty() || forward.req_name != serv_entry.first || forward.serv_instance.empty()) continue;
                found = true;
                bool guarded = is_arrayed && forward.req_port_index >= 0;
                if (guarded) impl_field.push_back("if constexpr (IDX == " + std::to_string(forward.req_port_index) + ") {\n");
                const auto &target = findChildTemplateByName(mod, forward.serv_instance);
                string slot;
                if (target.services.at(forward.serv_name).is_arrayed)
                    slot = "<" + (forward.serv_port_index >= 0 ? std::to_string(forward.serv_port_index) : (is_arrayed ? "IDX" : "0")) + ">";
                impl_field.push_back((rettype == "void" ? "" : "return ") + childPtrFieldName(mod, forward.serv_instance) + "->" + forward.serv_name + slot + "(" + argnames + ");\n");
                if (guarded) impl_field.push_back("}\n");
            }
            if (!found) throw VulException("Missing forwarding service: " + serv_entry.first);
            if (rettype != "void") impl_field.push_back("throw std::out_of_range(\"Service index out of range\");\n");
            impl_field.push_back("}\n");
        }
        decl_public_field.push_back("\n");
        impl_field.push_back("\n");
    }

    // generate query declarations
    for (const auto &query_entry : mod.queries) {
        const string &query_name = query_entry.first;
        const auto &query = query_entry.second;
        auto lb_iter = mod.query_logic_blocks.find(query_name);
        if (lb_iter == mod.query_logic_blocks.end()) {
            throw VulException("Missing logic block for query " + query_name);
        }

        decl_public_field.push_back(query.ret_type.toString() + " " + query_name + "() const;\n");
        impl_field.push_back(query.ret_type.toString() + " " + mod_class_name + "::" + query_name + "() const {\n");
        vulDebugAppendLines(impl_field, impl_field_debug, lowerPortIndexDims(lb_iter->second.codelines, {}, mod.connection_config), lb_iter->second.codelines_debug);
        impl_field.push_back("}\n");
        impl_field.push_back("\n");
    }

}

} // namespace detail

} // namespace simgen
