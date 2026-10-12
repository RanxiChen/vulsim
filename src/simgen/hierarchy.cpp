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
#include <map>
#include <set>

namespace simgen {
namespace detail {

void emitHierarchy(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    auto &decl_include_field = ctx.decl_include_field;
    auto &decl_public_field = ctx.decl_public_field;
    auto &decl_private_field = ctx.decl_private_field;
    auto &impl_init_field = ctx.impl_init_field;
    auto &impl_commit_field = ctx.impl_commit_field;
    auto &impl_sys_reset_field = ctx.impl_sys_reset_field;
    // Each physical child has a distinct type, while its declaration path shares files.
    std::set<string> includes;
    for (const auto &child : mod.children) {
        const string name = child->instance_path.back();
        if (includes.insert(child->simDeclPath()).second)
            decl_include_field.push_back("#include \"" + child->simDeclPath() + "\"\n");
        const string type = child->simConcreteClassName();
        const string field = childPtrFieldName(name, {});
        decl_private_field.push_back("std::unique_ptr<" + type + "> " + field + ";\n");
        impl_init_field.push_back(field + " = std::make_unique<" + type + ">(this);\n");
        impl_commit_field.push_back(field + "->" + ApplyTickFunctionName + "();\n");
        impl_sys_reset_field.push_back(field + "->reset();\n");
        for (const auto &[portname, req] : child->requests) {
            if (req.is_arrayed) decl_public_field.push_back("template <uint32_t IDX = 0>\n");
            const string rettype = req.returnType();
            decl_public_field.push_back(rettype + " __wrapper_" + name + "_" + portname + "(" + req.signatureArgOnly() + ") {\n");
            bool connected = false;
            for (const auto &conn : mod.req_connections) {
                if (conn.req_instance != name || conn.req_name != portname) continue;
                connected = true;
                const bool guarded = conn.req_port_index >= 0 && req.is_arrayed;
                if (guarded) decl_public_field.push_back("if constexpr (IDX == " + std::to_string(conn.req_port_index) + ") {\n");
                const auto &target = conn.serv_instance.empty() ? mod : findChildTemplateByName(mod, conn.serv_instance);
                const bool request = conn.serv_instance.empty() && target.requests.contains(conn.serv_name);
                const auto &dst = request ? target.requests.at(conn.serv_name) : target.services.at(conn.serv_name);
                string call = conn.serv_instance.empty() ? "" : childPtrFieldName(mod, conn.serv_instance) + "->";
                call += request && dst.is_arrayed ? portFlatName(conn.serv_name) : conn.serv_name;
                if (dst.is_arrayed) call += "<" + (conn.serv_port_index >= 0 ? std::to_string(conn.serv_port_index) : (req.is_arrayed ? "IDX" : "0")) + ">";
                decl_public_field.push_back((rettype == "void" ? "" : "return ") + call + "(" + req.signatureArgNameList() + ");\n");
                if (guarded) decl_public_field.push_back("}\n");
            }
            if (!connected) throw VulException("Unconnected concrete request: " + name + "." + portname);
            if (rettype != "void") decl_public_field.push_back("throw std::out_of_range(\"Request index out of range\");\n");
            decl_public_field.push_back("}\n");
        }
    }

    // exported child services / implicit requests
    std::map<string, vector<VulStaticChildServiceUse>> child_service_groups;
    for (const auto &use : mod.child_service_uses) {
        child_service_groups[use.alias_name].push_back(use);
    }
    for (const auto &group_entry : child_service_groups) {
        const string &alias_name = group_entry.first;
        const auto &group = group_entry.second;
        const auto &target_child = findChildTemplateByName(mod, group.front().instance_name);
        auto serv_iter = target_child.services.find(group.front().service_name);
        if (serv_iter == target_child.services.end()) {
            throw VulException("Service " + group.front().service_name + " not found in child " + target_child.module_name);
        }
        const auto &serv = serv_iter->second;
        string rettype = serv.returnType();
        string arglists = serv.signatureArgOnly();
        string argname = serv.signatureArgNameList();
        const bool is_alias_arrayed = group.front().alias_indexed;
        auto alias_port = serv;
        alias_port.name = alias_name; alias_port.port_dims = mod.child_service_shapes.at(alias_name);
        alias_port.is_arrayed = is_alias_arrayed; alias_port.array_size = group.size();
        const string callname = is_alias_arrayed ? portFlatName(alias_name) : alias_name;
        if (is_alias_arrayed) {
            decl_private_field.push_back("template <uint32_t IDX>\n");
        }
        decl_private_field.push_back(rettype + " " + callname + "(" + arglists + ") {\n");
        string call_prefix = (rettype == "void" ? "" : "return ");
        if (!is_alias_arrayed) {
            decl_private_field.push_back(CodeTab + call_prefix + childPtrFieldName(mod, group.front().instance_name) + "->" + group.front().service_name + "(" + argname + ");\n");
        } else {
            decl_private_field.push_back(CodeTab + "static_assert(IDX < " + std::to_string(group.size()) + ", \"Implicit request index out of range\");\n");
            for (size_t i = 0; i < group.size(); ++i) {
                const auto &use = group[i];
                string branch = (i == 0 ? "if constexpr" : "else if constexpr");
                decl_private_field.push_back(CodeTab + branch + " (IDX == " + std::to_string(use.alias_index) + ") {\n");
                decl_private_field.push_back(CodeTab + CodeTab + call_prefix + childPtrFieldName(mod, use.instance_name) + "->" + use.service_name + (serv.is_arrayed ? "<" + std::to_string(use.service_port_index) + ">" : "") + "(" + argname + ");\n");
                decl_private_field.push_back(CodeTab + "}\n");
            }
        }
        decl_private_field.push_back("}\n");
        decl_private_field.push_back(portRequestFacade(alias_port));
    }

    std::map<string, vector<VulStaticChildQueryUse>> child_query_groups;
    for (const auto &use : mod.child_query_uses) {
        child_query_groups[use.alias_name].push_back(use);
    }
    for (const auto &group_entry : child_query_groups) {
        const string &alias_name = group_entry.first;
        const auto &group = group_entry.second;
        const auto &target_child = findChildTemplateByName(mod, group.front().instance_name);
        auto query_iter = target_child.queries.find(group.front().query_name);
        if (query_iter == target_child.queries.end()) {
            throw VulException("Query " + group.front().query_name + " not found in child " + target_child.module_name);
        }
        if (!(group.front().ret_type == query_iter->second.ret_type)) {
            throw VulException("USE_CHILD_QUERY return type mismatch for alias " + alias_name);
        }
        const auto &shape = group.front().return_dims;
        const string rettype = portArrayType(query_iter->second.ret_type.toString(), shape);
        decl_private_field.push_back(rettype + " " + alias_name + "() const {\n");
        if (shape.empty()) {
            decl_private_field.push_back(CodeTab + "return " + childPtrFieldName(mod, group.front().instance_name) + "->" + group.front().query_name + "();\n");
        } else {
            decl_private_field.push_back(CodeTab + rettype + " values{};\n");
            for (const auto &use : group) {
                decl_private_field.push_back(CodeTab + "values" + portSubscript(use.alias_index, shape) + " = " +
                                            childPtrFieldName(mod, use.instance_name) + "->" + use.query_name + "();\n");
            }
            decl_private_field.push_back(CodeTab + "return values;\n");
        }
        decl_private_field.push_back("}\n");
    }

}

} // namespace detail

} // namespace simgen
