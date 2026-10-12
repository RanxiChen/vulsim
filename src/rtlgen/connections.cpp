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
#include "port_shape.h"
#include <map>

namespace rtlgen {
namespace detail {

namespace {
const VulStaticModuleInstance &findChildTemplateByName(
    const VulStaticModuleInstance &mod,
    const string &name
) {
    for (const auto &child : mod.children) {
        if (child->instance_path.back() == name) {
            return *child;
        }
    }
    throw VulException("Child instance template not found: " + name);
}

string childQuerySignalBase(const string &instance_name, const string &query_name) {
    return instance_name + "_" + query_name;
}

string childServiceSignalBase(const string &instance_name, const string &service_name) {
    return instance_name + "_" + service_name;
}

} // namespace
void _procChildrenAndConnection(RTLGenContext &ctx) {

    // CR-CS 需要wire，子实例-子实例
    // CR-S 需要wire，子实例-逻辑子模块
    // CR-R 不需要wire，直接从子实例端口连到模块端口
    // S-CS 不需要wire，直接从子实例端口连到模块端口
    // L-CS 需要wire，逻辑子模块-子实例

    // Concrete child ports and wiring use the same endpoint expansion as analysis.
    auto childNamed = [&](const string &name) -> const VulStaticModuleInstance & {
        for (const auto &child : ctx.module.children) if (child->instance_path.back() == name) return *child;
        throw VulException("Concrete RTL child not found: " + name);
    };
    for (const auto &ptr : ctx.module.children) {
        const auto &child = *ptr;
        const string name = child.instance_path.back();
        vector<string> bindings{ ".clk(clk)", ".rstn(rstn)" };
        auto emitPorts = [&](const auto &ports, bool service) {
            for (const auto &[portname, port] : ports) {
                const bool internal = service && std::any_of(child.concrete_connections.begin(), child.concrete_connections.end(), [&](const auto &conn) {
                    return !conn.req_instance.empty() && conn.serv_instance.empty() && conn.serv_name == portname;
                });
                if (internal) continue;
                const string base = childServiceSignalBase(name, portname);
                const string dims = port.is_arrayed ? "[" + std::to_string(port.array_size) + "]" : "";
                const bool logic_ref = service && std::any_of(ctx.module.child_service_uses.begin(), ctx.module.child_service_uses.end(), [&](const auto &use) {
                    return use.instance_name == name && use.service_name == portname;
                });
                auto arrayType = [&](string type) { return port.is_arrayed ? "std::array<" + type + ", " + std::to_string(port.array_size) + ">" : type; };
                auto emit = [&](const string &childport, const string &signal, uint32_t width, bool hls_input) {
                    ctx.rtl_decl.push_back("wire " + (width == 1 ? "" : "[" + std::to_string(width - 1) + ":0] ") + signal + dims + ";\n");
                    bindings.push_back("." + childport + "(" + signal + ")");
                    if (logic_ref) {
                        const bool boolean = childport == reqservVldPort(portname) || childport == reqservRdyPort(portname);
                        const string type = arrayType(boolean ? "bool" : "Int<" + std::to_string(width) + ">");
                        ctx.hls_arguments.push_back((hls_input ? "const " : "") + type + " & " + signal);
                        ctx.rtl_logicports.push_back("." + signal + "(" + signal + ")");
                    }
                };
                emit(reqservVldPort(portname), reqservVldPort(base), 1, false);
                if (port.has_handshake) emit(reqservRdyPort(portname), reqservRdyPort(base), 1, true);
                for (const auto &arg : port.args) emit(reqservArgPort(portname, arg.name), reqservArgPort(base, arg.name), procArg(arg, ctx.local_bundlelib).width, false);
                for (const auto &ret : port.rets) emit(reqservArgPort(portname, ret.name), reqservArgPort(base, ret.name), procArg(ret, ctx.local_bundlelib).width, true);
                if (logic_ref) {
                    for (uint32_t idx = 0; idx < port.array_size; ++idx) {
                        const string suffix = port.is_arrayed ? "[" + std::to_string(idx) + "]" : "";
                        ctx.hls_init.push_back(reqservVldPort(base) + suffix + " = false;\n");
                        for (const auto &arg : port.args) ctx.hls_init.push_back(reqservArgPort(base, arg.name) + suffix + " = 0;\n");
                    }
                } else if (service) {
                    for (uint32_t idx = 0; idx < port.array_size; ++idx) {
                        const bool connected = std::any_of(ctx.module.req_connections.begin(), ctx.module.req_connections.end(), [&](const auto &conn) {
                            return conn.serv_instance == name && conn.serv_name == portname && (conn.serv_port_index < 0 || uint32_t(conn.serv_port_index) == idx);
                        });
                        if (!connected) {
                            const string suffix = port.is_arrayed ? "[" + std::to_string(idx) + "]" : "";
                            ctx.rtl_logic.push_back("assign " + reqservVldPort(base) + suffix + " = 1'b0;\n");
                            for (const auto &arg : port.args) ctx.rtl_logic.push_back("assign " + reqservArgPort(base, arg.name) + suffix + " = '0;\n");
                        }
                    }
                }
            }
        };
        emitPorts(child.requests, false);
        emitPorts(child.services, true);
        for (const auto &[queryname, query] : child.queries) {
            const string signal = queryPort(childQuerySignalBase(name, queryname));
            uint32_t width = 0; vector<FlatField> fields;
            flatten_type_signature(query.ret_type, ctx.local_bundlelib, "value", width, fields);
            ctx.rtl_decl.push_back("wire [" + std::to_string(width - 1) + ":0] " + signal + ";\n");
            bindings.push_back("." + queryPort(queryname) + "(" + signal + ")");
            if (std::any_of(ctx.module.child_query_uses.begin(), ctx.module.child_query_uses.end(), [&](const auto &use) { return use.instance_name == name && use.query_name == queryname; })) {
                ctx.hls_arguments.push_back("const Int<" + std::to_string(width) + "> " + signal);
                ctx.rtl_logicports.push_back("." + signal + "(" + signal + ")");
            }
        }
        string parameters;
        auto coords = child.coordinateContext();
        for (size_t idx = 0; idx < coords.size(); ++idx) {
            if (idx) parameters += ", ";
            parameters += ".__vul_idx_" + std::to_string(idx) + "(" + std::to_string(coords[idx]) + ")";
        }
        ctx.rtl_inst.push_back(child.simClassName() + (parameters.empty() ? "" : " #(" + parameters + ")") + " " + name + " (\n");
        for (size_t idx = 0; idx < bindings.size(); ++idx) ctx.rtl_inst.push_back("  " + bindings[idx] + (idx + 1 == bindings.size() ? "" : ",") + "\n");
        ctx.rtl_inst.push_back(");\n");
    }
    for (const auto &conn : ctx.module.req_connections) {
        const auto &source = conn.req_instance.empty() ? ctx.module.services.at(conn.req_name) : childNamed(conn.req_instance).requests.at(conn.req_name);
        const auto &dest = conn.serv_instance.empty()
            ? (ctx.module.requests.contains(conn.serv_name) ? ctx.module.requests.at(conn.serv_name) : ctx.module.services.at(conn.serv_name))
            : childNamed(conn.serv_instance).services.at(conn.serv_name);
        const string srcbase = conn.req_instance.empty() ? conn.req_name : childServiceSignalBase(conn.req_instance, conn.req_name);
        const string dstbase = conn.serv_instance.empty() ? conn.serv_name : childServiceSignalBase(conn.serv_instance, conn.serv_name);
        uint32_t count = conn.req_port_index >= 0 ? 1 : source.array_size;
        for (uint32_t idx = 0; idx < count; ++idx) {
            const uint32_t si = conn.req_port_index >= 0 ? conn.req_port_index : idx;
            const uint32_t di = conn.serv_port_index >= 0 ? conn.serv_port_index : idx;
            const string ss = source.is_arrayed ? "[" + std::to_string(si) + "]" : "";
            const string ds = dest.is_arrayed ? "[" + std::to_string(di) + "]" : "";
            auto wire = [&](string dst, string src) { ctx.rtl_logic.push_back("assign " + dst + " = " + src + ";\n"); };
            wire(reqservVldPort(dstbase) + ds, reqservVldPort(srcbase) + ss);
            if (source.has_handshake) wire(reqservRdyPort(srcbase) + ss, reqservRdyPort(dstbase) + ds);
            for (size_t a = 0; a < source.args.size(); ++a) wire(reqservArgPort(dstbase, dest.args[a].name) + ds, reqservArgPort(srcbase, source.args[a].name) + ss);
            for (size_t r = 0; r < source.rets.size(); ++r) wire(reqservArgPort(srcbase, source.rets[r].name) + ss, reqservArgPort(dstbase, dest.rets[r].name) + ds);
        }
    }

    std::map<string, vector<VulStaticChildServiceUse>> child_service_groups;
    for (const auto &use : ctx.module.child_service_uses) {
        child_service_groups[use.alias_name].push_back(use);
    }
    for (const auto &group_entry : child_service_groups) {
        const string &alias_name = group_entry.first;
        const auto &group = group_entry.second;
        const auto &target_child = findChildTemplateByName(ctx.module, group.front().instance_name);
        auto serv_iter = target_child.services.find(group.front().service_name);
        if (serv_iter == target_child.services.end()) {
            throw VulException("Service " + group.front().service_name + " not found in child " + target_child.module_name);
        }
        const auto &serv = serv_iter->second;
        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;
        for (const auto &arg : serv.args) {
            arg_ports.push_back(procArg(arg, ctx.local_bundlelib));
        }
        for (const auto &ret : serv.rets) {
            ret_ports.push_back(procArg(ret, ctx.local_bundlelib));
        }


            const bool is_alias_arrayed = group.front().alias_indexed;
            auto alias_port = serv;
            alias_port.name = alias_name; alias_port.port_dims = ctx.module.child_service_shapes.at(alias_name);
            alias_port.is_arrayed = is_alias_arrayed; alias_port.array_size = group.size();
            if (!is_alias_arrayed && serv.returnType() == "void") {
                const string signal_base = childServiceSignalBase(group.front().instance_name, group.front().service_name);
                vector<string> arg_macro_names;
                vector<string> ret_macro_names;
                string macro_arg_list;
                arg_macro_names.resize(arg_ports.size());
                ret_macro_names.resize(ret_ports.size());
                for (const auto &param : serv.param_order) {
                    const bool is_arg = param.kind == VulReqServParamKind::Arg;
                    const string macro_name = is_arg
                        ? "__vul_child_arg_" + std::to_string(param.index)
                        : "__vul_child_ret_" + std::to_string(param.index);
                    if (!macro_arg_list.empty()) macro_arg_list += ", ";
                    macro_arg_list += macro_name;
                    if (is_arg) {
                        arg_macro_names.at(param.index) = "(" + macro_name + ")";
                    } else {
                        ret_macro_names.at(param.index) = "(" + macro_name + ")";
                    }
                }
                ctx.hls_header.push_back("#define " + alias_name + "(" + macro_arg_list + ") { \\\n");
                ctx.hls_header.push_back("  " + reqservVldPort(signal_base) + " = true; \\\n");
                for (size_t i = 0; i < arg_ports.size(); ++i) {
                    const auto &arg = arg_ports[i];
                    const string arg_port_name = reqservArgPort(signal_base, arg.name);
                    for (const auto &field : arg.flat_fields) {
                        const string value_expr = reqservFieldValueExpr(arg_macro_names[i], arg.name, field.name);
                        ctx.hls_header.push_back("  " + uintExtractExpr(arg_port_name, field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, value_expr) + "; \\\n");
                    }
                }
                for (size_t i = 0; i < ret_ports.size(); ++i) {
                    const auto &ret = ret_ports[i];
                    const string ret_port_name = reqservArgPort(signal_base, ret.name);
                    for (const auto &field : ret.flat_fields) {
                        const string lvalue_expr = reqservFieldValueExpr(ret_macro_names[i], ret.name, field.name);
                        const string value_expr = apiinline::unpackFlatFieldValueExpr(
                            lvalue_expr,
                            uintExtractExpr(ret_port_name, field.offset + field.width - 1, field.offset),
                            field);
                        ctx.hls_header.push_back("  " + lvalue_expr + " = " + value_expr + "; \\\n");
                    }
                }
                ctx.hls_header.push_back("}\n");
                ctx.hls_final.push_back("#undef " + alias_name + "\n");
                continue;
            }
            const string v2_arglists = reqservValueArgList(serv);
            if (is_alias_arrayed) {
                ctx.hls_helpers.push_back("auto " + portFlatName(alias_name) + " = [&]<uint32_t IDX>(" + v2_arglists + ") -> " + serv.returnType() + " {\n");
                ctx.hls_helpers.push_back("  static_assert(IDX < " + std::to_string(group.size()) + ", \"Implicit request index out of range\");\n");
            } else {
                ctx.hls_helpers.push_back("auto " + alias_name + " = [&](" + v2_arglists + ") -> " + serv.returnType() + " {\n");
            }
            for (size_t i = 0; i < group.size(); ++i) {
                const string signal_base = childServiceSignalBase(group[i].instance_name, group[i].service_name);
                const string selected = serv.is_arrayed ? "[" + std::to_string(group[i].service_port_index) + "]" : "";
                string indent = "  ";
                if (is_alias_arrayed) {
                    string branch = (i == 0 ? "if constexpr" : "else if constexpr");
                    ctx.hls_helpers.push_back("  " + branch + " (IDX == " + std::to_string(group[i].alias_index) + ") {\n");
                    indent = "    ";
                }
                ctx.hls_helpers.push_back(indent + reqservVldPort(signal_base) + selected + " = true;\n");
                for (const auto &arg : arg_ports) {
                    const string arg_port_name = reqservArgPort(signal_base, arg.name);
                    for (const auto &field : arg.flat_fields) {
                        ctx.hls_helpers.push_back(indent + uintExtractExpr(arg_port_name + selected, field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, field.name) + ";\n");
                    }
                }
                for (const auto &ret : ret_ports) {
                    const string ret_port_name = reqservArgPort(signal_base, ret.name);
                    for (const auto &field : ret.flat_fields) {
                        ctx.hls_helpers.push_back(indent + field.name + " = " + typedExtractExpr(field, ret_port_name + selected) + ";\n");
                    }
                }
                if (serv.has_handshake) {
                    ctx.hls_helpers.push_back(indent + "return " + reqservRdyPort(signal_base) + selected + ";\n");
                } else if (serv.returnType() == "void") {
                    ctx.hls_helpers.push_back(indent + "return;\n");
                }
                if (is_alias_arrayed) {
                    ctx.hls_helpers.push_back("  }\n");
                }
                if (!is_alias_arrayed) {
                    break;
                }
            }
            ctx.hls_helpers.push_back("};\n");
            if (is_alias_arrayed) {
                ctx.hls_helpers.push_back("auto __vul_bulk_" + alias_name + " = [&](" + portBulkArguments(alias_port) + ") -> " + portArrayType(serv.returnType(), alias_port.port_dims) + " {\n");
                ctx.hls_helpers.push_back(portBulkBody(alias_port, portFlatName(alias_name), true));
                ctx.hls_helpers.push_back("};\n");
            }

    }

    std::map<string, vector<VulStaticChildQueryUse>> child_query_groups;
    for (const auto &use : ctx.module.child_query_uses) {
        child_query_groups[use.alias_name].push_back(use);
    }
    for (const auto &group_entry : child_query_groups) {
        const string &alias_name = group_entry.first;
        const auto &group = group_entry.second;
        const auto &target_child = findChildTemplateByName(ctx.module, group.front().instance_name);
        auto query_iter = target_child.queries.find(group.front().query_name);
        if (query_iter == target_child.queries.end()) {
            throw VulException("Query " + group.front().query_name + " not found in child " + target_child.module_name);
        }
        if (!(group.front().ret_type == query_iter->second.ret_type)) {
            throw VulException("USE_CHILD_QUERY return type mismatch for alias " + alias_name);
        }

        uint32_t width = 0;
        vector<FlatField> flat_fields;
        flatten_type_signature(query_iter->second.ret_type, ctx.local_bundlelib, "value", width, flat_fields);


        const auto &shape = group.front().return_dims;
        const string element_type = query_iter->second.ret_type.toString();
        const string result_type = portArrayType(element_type, shape);
        ctx.hls_helpers.push_back("auto " + alias_name + " = [&]() -> " + result_type + " {\n");
        if (!shape.empty()) ctx.hls_helpers.push_back("  " + result_type + " values{};\n");
        for (const auto &use : group) {
            const string value_name = queryPort(childQuerySignalBase(use.instance_name, use.query_name));
            // Use a separate scalar temporary for each child so struct, enum,
            // Int and existing array return types all share the same unpacking.
            ctx.hls_helpers.push_back("  {\n    " + element_type + " value = " +
                                      apiinline::defaultValueExprForType(query_iter->second.ret_type, ctx.local_bundlelib) + ";\n");
            for (const auto &field : flat_fields)
                ctx.hls_helpers.push_back("    " + field.name + " = " + typedExtractExpr(field, value_name) + ";\n");
            if (shape.empty()) ctx.hls_helpers.push_back("    return value;\n");
            else ctx.hls_helpers.push_back("    values" + portSubscript(use.alias_index, shape) + " = value;\n");
            ctx.hls_helpers.push_back("  }\n");
        }
        if (!shape.empty()) ctx.hls_helpers.push_back("  return values;\n");
        ctx.hls_helpers.push_back("};\n");

    }
}


} // namespace detail

} // namespace rtlgen
