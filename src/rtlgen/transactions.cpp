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
#include "debugmap.hpp"
#include <algorithm>
#include <sstream>

namespace rtlgen {
namespace detail {
void _procRequests(RTLGenContext &ctx) {

    auto request_driven_by_child_connection = [&](const string &req_name) {
        for (const auto &conn : ctx.module.req_connections) {
            if (!conn.req_instance.empty() && conn.serv_instance.empty() && conn.serv_name == req_name) {
                return true;
            }
        }
        return false;
    };

    for (const auto &req_entry : ctx.module.requests) {
        const string &req_name = req_entry.first;
        const VulStaticReqServ &req = req_entry.second;

        VulErrorContextGuard req_guard("processing request " + req_name);

        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;

        for (const auto &arg : req.args) {
            arg_ports.push_back(procArg(arg, ctx.local_bundlelib));
        }
        for (const auto &ret : req.rets) {
            ret_ports.push_back(procArg(ret, ctx.local_bundlelib));
        }

        string rettype = req.returnType();
        string arglists = req.signatureArgOnly();
        bool has_rdy = req.has_handshake;
        const bool is_arrayed = req.is_arrayed;
        const uint32_t array_size = static_cast<uint32_t>(req.array_size);
        const bool connected_from_child_request = request_driven_by_child_connection(req_name);

        auto arrayed_port_decl = [&](const string &base_name) -> string {
            if (is_arrayed) {
                return "std::array<" + base_name + ", " + std::to_string(array_size) + ">";
            } else {
                return base_name;
            }
        };

        // hls function ports
        string vld_port_name = reqservVldPort(req_name);
        string rdy_port_name = reqservRdyPort(req_name);
        if (!connected_from_child_request) {
            ctx.hls_arguments.push_back(arrayed_port_decl("bool") + " &" + vld_port_name);
            if (has_rdy) {
                ctx.hls_arguments.push_back("const " + arrayed_port_decl("bool") + " " + rdy_port_name);
            }
            for (const auto &arg : arg_ports) {
                ctx.hls_arguments.push_back(arrayed_port_decl("Int<" + std::to_string(arg.width) + ">") + " &" + reqservArgPort(req_name, arg.name));
            }
            for (const auto &ret : ret_ports) {
                ctx.hls_arguments.push_back("const " + arrayed_port_decl("Int<" + std::to_string(ret.width) + ">") + " " + reqservArgPort(req_name, ret.name));
            }

            if (is_arrayed) {
                for (uint32_t idx = 0; idx < array_size; ++idx) {
                    ctx.hls_init.push_back(vld_port_name + "[" + std::to_string(idx) + "] = false;\n");
                }
            } else {
                ctx.hls_init.push_back(vld_port_name + " = false;\n");
            }
            for (const auto &arg : arg_ports) {
                const string arg_port_name = reqservArgPort(req_name, arg.name);
                if (is_arrayed) {
                    for (uint32_t idx = 0; idx < array_size; ++idx) {
                        ctx.hls_init.push_back(arg_port_name + "[" + std::to_string(idx) + "] = 0;\n");
                    }
                } else {
                    ctx.hls_init.push_back(arg_port_name + " = 0;\n");
                }
            }


        }

        // generate RTL ports
        string sv_array_str = is_arrayed ? ("[" + std::to_string(array_size) + "]") : "";
        ctx.rtl_ports.push_back("output " + vld_port_name + sv_array_str);
        if (!connected_from_child_request) {
            ctx.rtl_logicports.push_back("." + vld_port_name + "(" + vld_port_name + ")");
        }
        if (has_rdy) {
            ctx.rtl_ports.push_back("input " + rdy_port_name + sv_array_str);
            if (!connected_from_child_request) {
                ctx.rtl_logicports.push_back("." + rdy_port_name + "(" + rdy_port_name + ")");
            }
        }
        for (const auto &arg : arg_ports) {
            string arg_port_name = reqservArgPort(req_name, arg.name);
            ctx.rtl_ports.push_back("output [" + std::to_string(arg.width - 1) + ":0] " + arg_port_name + sv_array_str);
            if (!connected_from_child_request) {
                ctx.rtl_logicports.push_back("." + arg_port_name + "(" + arg_port_name + ")");
            }
        }
        for (const auto &ret : ret_ports) {
            string ret_port_name = reqservArgPort(req_name, ret.name);
            ctx.rtl_ports.push_back("input [" + std::to_string(ret.width - 1) + ":0] " + ret_port_name + sv_array_str);
            if (!connected_from_child_request) {
                ctx.rtl_logicports.push_back("." + ret_port_name + "(" + ret_port_name + ")");
            }
        }
    }
}

void _procServicesAndTicks(RTLGenContext &ctx) {

    struct ServiceCache {
        ReqServName name;
        string rettype;
        string argtypes;
        string argnames;
        string arglists;
        bool has_rdy;
        bool is_arrayed = false;
        uint32_t array_size = 1;
        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;
        bool is_tick = false;
    };

    auto condFuncName = [](const string &serv_name) {
        return serv_name + "_cond__";
    };
    auto implFuncName = [](const string &serv_name) {
        return serv_name + "_impl__";
    };

    map<int32_t, vector<ServiceCache>> service_cache; // priority -> service cache

    auto emit_unpack_arg = [&](const ArgPort &arg, const string &portname) {
        string typestr = arg.type.toString();
        ctx.hls_body.push_back("  " + typestr + " " + arg.name + " = " +
                               apiinline::defaultValueExprForType(arg.type, ctx.local_bundlelib) + ";\n");
        for (const auto &field : arg.flat_fields) {
            ctx.hls_body.push_back("  " + field.name + " = " + typedExtractExpr(field, portname) + ";\n");
        }
    };

    auto emit_construct_ret = [&](const ArgPort &ret) {
        string typestr = ret.type.toString();
        ctx.hls_body.push_back("  " + typestr + " " + ret.name + " = " +
                               apiinline::defaultValueExprForType(ret.type, ctx.local_bundlelib) + ";\n");
    };

    auto emit_pack_ret = [&](const ArgPort &ret, const string &portname) {
        string temp_var_name = ret.name + "_packed";
        ctx.hls_body.push_back("    Int<" + std::to_string(ret.width) + "> " + ret.name + "_packed = 0;\n");
        for (const auto &field : ret.flat_fields) {
            ctx.hls_body.push_back("    " + uintExtractExpr(temp_var_name, field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, field.name) + ";\n");
        }
        ctx.hls_body.push_back("    " + portname + " = " + temp_var_name + ";\n");
    };

    for (const auto &entry : ctx.module.services) {
        const string &serv_name = entry.first;
        const VulStaticReqServ &serv = entry.second;

        VulErrorContextGuard serv_guard("processing service " + serv_name);

        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;

        for (const auto &arg : serv.args) {
            arg_ports.push_back(procArg(arg, ctx.local_bundlelib));
        }
        for (const auto &ret : serv.rets) {
            ret_ports.push_back(procArg(ret, ctx.local_bundlelib));
        }

        string rettype = serv.returnType();
        string argtypes = serv.signatureArgTypeOnly();
        string argnames = serv.signatureArgNameList();
        string arglists = serv.signatureArgOnly();
        arglists = reqservValueArgList(serv);
        bool has_rdy = serv.has_handshake;
        const bool is_arrayed = serv.is_arrayed;
        const uint32_t array_size = static_cast<uint32_t>(serv.array_size);
        string sv_array_str = is_arrayed ? ("[" + std::to_string(array_size) + "]") : "";

        auto arrayed_port_decl = [&](const string &base_name) -> string {
            if (is_arrayed) {
                return "std::array<" + base_name + ", " + std::to_string(array_size) + ">";
            } else {
                return base_name;
            }
        };

        // A service driven by a local child's request is an internal transaction,
        // not an additional module input. Keep the logic ABI but declare local wires.
        const bool internal = std::any_of(ctx.module.req_connections.begin(), ctx.module.req_connections.end(),
            [&](const auto &conn) {
                return !conn.req_instance.empty() && conn.serv_instance.empty() && conn.serv_name == serv_name;
            });
        auto emit_service_port = [&](const string &direction, const string &signal) {
            if (internal) ctx.rtl_decl.push_back("wire " + signal + ";\n");
            else ctx.rtl_ports.push_back(direction + " " + signal);
        };
        // rtl ports
        string vld_port_name = reqservVldPort(serv_name);
        string rdy_port_name = reqservRdyPort(serv_name);
        emit_service_port("input", vld_port_name + sv_array_str);
        if (has_rdy) {
            emit_service_port("output", rdy_port_name + sv_array_str);
        }
        for (const auto &arg : arg_ports) {
            string arg_port_name = reqservArgPort(serv_name, arg.name);
            emit_service_port("input", "[" + std::to_string(arg.width - 1) + ":0] " + arg_port_name + sv_array_str);
        }
        for (const auto &ret : ret_ports) {
            string ret_port_name = reqservArgPort(serv_name, ret.name);
            emit_service_port("output", "[" + std::to_string(ret.width - 1) + ":0] " + ret_port_name + sv_array_str);
        }

        // a service should be:
        // (1) implemented in logic code blocks
        // (2) connected to child instance service ports

        auto iter = ctx.module.serv_logic_blocks.find(serv_name);
        if (iter == ctx.module.serv_logic_blocks.end()) {
            continue;
        }
        const auto& lb = iter->second;

        ctx.hls_arguments.push_back("const " + arrayed_port_decl("bool") + " " + vld_port_name);
        ctx.rtl_logicports.push_back("." + vld_port_name + "(" + vld_port_name + ")");
        if (has_rdy) {
            ctx.hls_arguments.push_back(arrayed_port_decl("bool") + " & " + rdy_port_name);
            ctx.rtl_logicports.push_back("." + rdy_port_name + "(" + rdy_port_name + ")");
        }
        for (const auto &arg : arg_ports) {
            string arg_port_name = reqservArgPort(serv_name, arg.name);
            ctx.hls_arguments.push_back("const " + arrayed_port_decl("Int<" + std::to_string(arg.width) + ">") + " " + arg_port_name);
            ctx.rtl_logicports.push_back("." + arg_port_name + "(" + arg_port_name + ")");
        }
        for (const auto &ret : ret_ports) {
            string ret_port_name = reqservArgPort(serv_name, ret.name);
            ctx.hls_arguments.push_back(arrayed_port_decl("Int<" + std::to_string(ret.width) + ">") + " &" + ret_port_name);
            ctx.rtl_logicports.push_back("." + ret_port_name + "(" + ret_port_name + ")");
            if (is_arrayed) {
                for (uint32_t idx = 0; idx < array_size; ++idx) {
                    ctx.hls_init.push_back(ret_port_name + "[" + std::to_string(idx) + "] = 0;\n");
                }
            } else {
                ctx.hls_init.push_back(ret_port_name + " = 0;\n");
            }
        }

        if (is_arrayed) {
            ctx.hls_blocks.push_back("auto " + implFuncName(serv_name) + " = [&]<uint32_t IDX = 0>(" + arglists + ") -> void {\n");
        } else {
            ctx.hls_blocks.push_back("auto " + implFuncName(serv_name) + " = [&](" + arglists + ") -> void {\n");
        }
        vulDebugAppendLines(ctx.hls_blocks, ctx.hls_blocks_debug, lowerPortIndexDims(lb.codelines, serv.port_dims, ctx.module.connection_config), lb.codelines_debug);
        ctx.hls_blocks.push_back("};\n");
        if (has_rdy) {
            if (is_arrayed) {
                ctx.hls_blocks.push_back("auto " + condFuncName(serv_name) + " = [&]<uint32_t IDX = 0>(" + arglists + ") -> bool {\n");
            } else {
                ctx.hls_blocks.push_back("auto " + condFuncName(serv_name) + " = [&](" + arglists + ") -> bool {\n");
            }
            vulDebugAppendLines(ctx.hls_blocks, ctx.hls_blocks_debug, lowerPortIndexDims(lb.cond_codelines, serv.port_dims, ctx.module.connection_config), lb.cond_codelines_debug);
            ctx.hls_blocks.push_back("};\n");
        }

        // cache service info for tick generation
        ServiceCache cache;
        cache.name = serv_name;
        cache.rettype = rettype;
        cache.argtypes = argtypes;
        cache.argnames = argnames;
        cache.arglists = arglists;
        cache.has_rdy = has_rdy;
        cache.is_arrayed = is_arrayed;
        cache.array_size = array_size;
        cache.arg_ports = arg_ports;
        cache.ret_ports = ret_ports;
        cache.is_tick = false;
        service_cache[lb.with_priority?lb.priority:0].push_back(cache);
    }
    // ticks
    uint32_t tick_count = 0;
    for (const auto &tb : ctx.module.tick_blocks) {
        string name = "tick" + std::to_string(tick_count++) + "__";
        ctx.hls_blocks.push_back("auto " + name + " = [&]() {\n");
        vulDebugAppendLines(ctx.hls_blocks, ctx.hls_blocks_debug, tb.codelines, tb.codelines_debug);
        ctx.hls_blocks.push_back("};\n");

        // cache tick info
        ServiceCache cache;
        cache.name = name;
        cache.rettype = "void";
        cache.argtypes = "";
        cache.argnames = "";
        cache.arglists = "";
        cache.has_rdy = false;
        cache.is_tick = true;
        service_cache[0].push_back(cache);
    }
    // generate cycle logic
    VulErrorContextGuard cycle_guard("generating cycle logic for services and ticks");
    for (auto it = service_cache.rbegin(); it != service_cache.rend(); ++it) {
        int32_t key = it->first;
        const std::vector<ServiceCache>& vec = it->second;
        for (const ServiceCache& cache : vec) {
            if (cache.has_rdy) {
                if (cache.is_arrayed) {
                    for (uint32_t idx = 0; idx < cache.array_size; ++idx) {
                        ctx.hls_body.push_back("{\n");
                        const string &call_names = cache.argnames;
                        for (uint32_t i = 0; i < cache.arg_ports.size(); ++i) {
                            const auto &arg = cache.arg_ports[i];
                            string portname = (reqservArgPort(cache.name, arg.name) + "[" + std::to_string(idx) + "]");
                            emit_unpack_arg(arg, portname);
                        }
                        for (const auto &ret : cache.ret_ports) {
                            emit_construct_ret(ret);
                        }
                        ctx.hls_body.push_back("  bool rdy = " + condFuncName(cache.name) + ".template operator()<" + std::to_string(idx) + ">(" + call_names + ");\n");
                        ctx.hls_body.push_back("  if (rdy && " + reqservVldPort(cache.name) + "[" + std::to_string(idx) + "]) {\n");
                        ctx.hls_body.push_back("    " + implFuncName(cache.name) + ".template operator()<" + std::to_string(idx) + ">(" + call_names + ");\n");
                        for (const auto &ret : cache.ret_ports) {
                            string portname = reqservArgPort(cache.name, ret.name) + "[" + std::to_string(idx) + "]";
                            emit_pack_ret(ret, portname);
                        }
                        ctx.hls_body.push_back("  }\n");
                        ctx.hls_body.push_back("  " + reqservRdyPort(cache.name) + "[" + std::to_string(idx) + "] = rdy;\n");
                        ctx.hls_body.push_back("}\n");
                    }
                } else {
                    ctx.hls_body.push_back("{\n");
                    const string &call_names = cache.argnames;
                    for (uint32_t i = 0; i < cache.arg_ports.size(); ++i) {
                        const auto &arg = cache.arg_ports[i];
                        string portname = (reqservArgPort(cache.name, arg.name));
                        emit_unpack_arg(arg, portname);
                    }
                    for (const auto &ret : cache.ret_ports) {
                        emit_construct_ret(ret);
                    }
                    ctx.hls_body.push_back("  bool rdy = " + condFuncName(cache.name) + "(" + call_names + ");\n");
                    ctx.hls_body.push_back("  if (rdy && " + reqservVldPort(cache.name) + ") {\n");
                    ctx.hls_body.push_back("    " + implFuncName(cache.name) + "(" + call_names + ");\n");
                    for (const auto &ret : cache.ret_ports) {
                        string portname = reqservArgPort(cache.name, ret.name);
                        emit_pack_ret(ret, portname);
                    }
                    ctx.hls_body.push_back("  }\n");
                    ctx.hls_body.push_back("  " + reqservRdyPort(cache.name) + " = rdy;\n");
                    ctx.hls_body.push_back("}\n");
                }
            } else if (!cache.is_tick) {
                if (cache.is_arrayed) {
                    for (uint32_t idx = 0; idx < cache.array_size; ++idx) {
                        ctx.hls_body.push_back("{\n");
                        const string &call_names = cache.argnames;
                        for (uint32_t i = 0; i < cache.arg_ports.size(); ++i) {
                            const auto &arg = cache.arg_ports[i];
                            string portname = reqservArgPort(cache.name, arg.name) + "[" + std::to_string(idx) + "]";
                            emit_unpack_arg(arg, portname);
                        }
                        for (const auto &ret : cache.ret_ports) {
                            emit_construct_ret(ret);
                        }
                        ctx.hls_body.push_back("  if (" + reqservVldPort(cache.name) + "[" + std::to_string(idx) + "]) {\n");
                        ctx.hls_body.push_back("    " + implFuncName(cache.name) + ".template operator()<" + std::to_string(idx) + ">(" + call_names + ");\n");
                        for (const auto &ret : cache.ret_ports) {
                            string portname = reqservArgPort(cache.name, ret.name) + "[" + std::to_string(idx) + "]";
                            emit_pack_ret(ret, portname);
                        }
                        ctx.hls_body.push_back("  }\n");
                        ctx.hls_body.push_back("}\n");
                    }
                } else {
                    ctx.hls_body.push_back("{\n");
                    const string &call_names = cache.argnames;
                    for (uint32_t i = 0; i < cache.arg_ports.size(); ++i) {
                        const auto &arg = cache.arg_ports[i];
                        string portname = reqservArgPort(cache.name, arg.name);
                        emit_unpack_arg(arg, portname);
                    }
                    for (const auto &ret : cache.ret_ports) {
                        emit_construct_ret(ret);
                    }
                    ctx.hls_body.push_back("  if (" + reqservVldPort(cache.name) + ") {\n");
                    ctx.hls_body.push_back("    " + implFuncName(cache.name) + "(" + call_names + ");\n");
                    for (const auto &ret : cache.ret_ports) {
                        string portname = reqservArgPort(cache.name, ret.name);
                        emit_pack_ret(ret, portname);
                    }
                    ctx.hls_body.push_back("  }\n");
                    ctx.hls_body.push_back("}\n");
                }
            } else {
                ctx.hls_body.push_back(cache.name + "();\n");
            }
        }
    }
}

void _procQueries(RTLGenContext &ctx) {
    for (const auto &entry : ctx.module.queries) {
        const string &query_name = entry.first;
        const auto &query = entry.second;

        auto iter = ctx.module.query_logic_blocks.find(query_name);
        if (iter == ctx.module.query_logic_blocks.end()) {
            throw VulException("Missing logic block for query " + query_name);
        }

        const string query_value = "__vul_query_value_" + query_name;
        uint32_t width = 0;
        vector<FlatField> flat_fields;
        flatten_type_signature(query.ret_type, ctx.local_bundlelib, query_value, width, flat_fields);

        const string port_name = queryPort(query_name);
        const string ret_type_str = query.ret_type.toString();

        ctx.hls_arguments.push_back("Int<" + std::to_string(width) + "> &" + port_name);
        ctx.rtl_ports.push_back("output [" + std::to_string(width - 1) + ":0] " + port_name);
        ctx.rtl_logicports.push_back("." + port_name + "(" + port_name + ")");

        ctx.hls_blocks.push_back("auto " + query_name + " = [&]() -> " + ret_type_str + " {\n");
        vulDebugAppendLines(ctx.hls_blocks, ctx.hls_blocks_debug, iter->second.codelines, iter->second.codelines_debug);
        ctx.hls_blocks.push_back("};\n");

        ctx.hls_body.push_back("{\n");
        ctx.hls_body.push_back("  " + ret_type_str + " " + query_value + " = " + query_name + "();\n");
        ctx.hls_body.push_back("  Int<" + std::to_string(width) + "> packed = 0;\n");
        for (const auto &field : flat_fields) {
            ctx.hls_body.push_back("  " + uintExtractExpr("packed", field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, field.name) + ";\n");
        }
        ctx.hls_body.push_back("  " + port_name + " = packed;\n");
        ctx.hls_body.push_back("}\n");
    }
}


} // namespace detail

} // namespace rtlgen
