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
#include <algorithm>
#include <optional>
#include <unordered_set>

VulStaticReqServ staticalizeReqServ(const VulTempReqServBase &item, const VulStaticConfigLib &config_lib) {
    VulStaticReqServ static_req;
    static_req.name = item.name;
    static_req.is_arrayed = false;
    static_req.array_size = 1;
    static_req.has_handshake = item.has_handshake;
    static_req.param_order = item.param_order;
    const auto &dims = item.port_dims;
    for (const auto &expr : dims) {
        auto dim = calculateConstexprValue(expr, config_lib);
        if (dim <= 0 || uint64_t(dim) > UINT32_MAX / uint64_t(static_req.array_size))
            throw VulException("Transaction dimensions must be positive and their product must fit uint32_t: " + item.name);
        static_req.port_dims.push_back(dim);
        static_req.array_size *= dim;
    }
    static_req.is_arrayed = !dims.empty();
    for (const auto &arg : item.args) {
        VulStaticArg static_arg;
        static_arg.name = arg.second;
        static_arg.type = parseTypeSignature(arg.first, config_lib);
        static_req.args.push_back(static_arg);
    }
    for (const auto &ret : item.rets) {
        VulStaticArg static_arg;
        static_arg.name = ret.second;
        static_arg.type = parseTypeSignature(ret.first, config_lib);
        static_req.rets.push_back(static_arg);
    }
    return static_req;
}

namespace vulmodule::detail {
namespace {
static bool sameServiceDeclarationSignature(
    const VulStaticReqServ &decl,
    const VulStaticReqServ &impl,
    string &reason
) {
    if (decl.has_handshake != impl.has_handshake) {
        reason = "handshake attribute differs";
        return false;
    }
    if (decl.is_arrayed != impl.is_arrayed) {
        reason = "array attribute presence differs";
        return false;
    }
    if (decl.port_dims != impl.port_dims) {
        reason = "array size differs";
        return false;
    }
    if (decl.args.size() != impl.args.size()) {
        reason = "ARG count differs";
        return false;
    }
    if (decl.rets.size() != impl.rets.size()) {
        reason = "RESP count differs";
        return false;
    }
    if (decl.param_order != impl.param_order) {
        reason = "ARG/RESP declaration order differs";
        return false;
    }
    for (size_t i = 0; i < decl.args.size(); ++i) {
        if (decl.args[i].name != impl.args[i].name) {
            reason = "ARG #" + std::to_string(i) + " name differs";
            return false;
        }
        if (decl.args[i].type != impl.args[i].type) {
            reason = "ARG '" + decl.args[i].name + "' type differs";
            return false;
        }
    }
    for (size_t i = 0; i < decl.rets.size(); ++i) {
        if (decl.rets[i].name != impl.rets[i].name) {
            reason = "RESP #" + std::to_string(i) + " name differs";
            return false;
        }
        if (decl.rets[i].type != impl.rets[i].type) {
            reason = "RESP '" + decl.rets[i].name + "' type differs";
            return false;
        }
    }
    return true;
}

static VulStaticQuery staticalizeQuery(const VulTempQuery &item, const VulStaticConfigLib &config_lib) {
    VulStaticQuery query;
    query.name = item.name;
    query.ret_type = parseTypeSignature(item.ret_type, config_lib);
    return query;
}

} // namespace

void staticalizeTransactions(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    auto &next_logic_block_id = ctx.next_logic_block_id;
    // Requests
    for (const auto &req : temp.requests) {
        VulErrorContextGuard _err{"Processing request '" + req.name + "'"};
        instance.requests[req.name] = staticalizeReqServ(req, local_config_lib);
    }

    // services
    struct ServiceDeclarationInfo {
        VulStaticReqServ signature;
        std::optional<ConfigRealValue> priority;
        bool implemented = false;
    };
    unordered_map<string, ServiceDeclarationInfo> service_declarations;
    for (const auto &serv : temp.services) {
        const string &serv_name = serv.name;
        VulErrorContextGuard _err{
            string("Processing service ") + (serv.is_declaration ? "declaration" : "implementation") +
            " '" + serv_name + "'"
        };
        VulStaticReqServ static_serv = staticalizeReqServ(serv, local_config_lib);
        std::optional<ConfigRealValue> priority_value;
        if (!serv.priority.empty()) {
            priority_value = calculateConstexprValue(serv.priority, local_config_lib);
        }
        if (serv.is_declaration) {
            if (service_declarations.find(serv_name) != service_declarations.end()) {
                throw VulException("Duplicate SERVICE declaration for '" + serv_name + "'");
            }
            ServiceDeclarationInfo info;
            info.signature = std::move(static_serv);
            info.priority = priority_value;
            service_declarations[serv_name] = std::move(info);
            continue;
        }

        auto decl_iter = service_declarations.find(serv_name);
        if (decl_iter != service_declarations.end()) {
            string reason;
            if (!sameServiceDeclarationSignature(decl_iter->second.signature, static_serv, reason)) {
                throw VulException("SERVICE implementation for '" + serv_name +
                                   "' does not match its forward declaration: " + reason);
            }
            if (decl_iter->second.priority.has_value()) {
                if (!priority_value.has_value()) {
                    throw VulException("SERVICE implementation for '" + serv_name +
                                       "' does not match its forward declaration: priority attribute is missing");
                }
                if (*decl_iter->second.priority != *priority_value) {
                    throw VulException("SERVICE implementation for '" + serv_name +
                                       "' does not match its forward declaration: priority value differs");
                }
            }
            decl_iter->second.implemented = true;
        }

        instance.services[serv_name] = static_serv;
        VulLogicBlock lb;
        lb.block_id = next_logic_block_id++;
        lb.with_priority = !serv.priority.empty();
        if (priority_value.has_value()) {
            lb.priority = *priority_value;
        } else {
            lb.priority = 0;
        }
        lb.codelines = serv.codelines;
        lb.codelines_debug = serv.codelines_debug;
        if (serv.has_handshake) {
            lb.cond_codelines.push_back("return (" + serv.cond + ");\n");
            lb.cond_codelines_debug.push_back(serv.cond_debug);
        }
        instance.serv_logic_blocks[serv_name] = lb;
    }
    for (const auto &entry : service_declarations) {
        if (!entry.second.implemented) {
            const bool forwarded = std::any_of(temp.req_connections.begin(), temp.req_connections.end(),
                [&](const auto &conn) {
                    return conn.req_instance.empty() && conn.req_name == entry.first && !conn.serv_instance.empty();
                });
            if (!forwarded) {
                throw VulException("SERVICE forward declaration for '" + entry.first + "' has no matching implementation");
            }
            // A boundary service can be implemented by CONNECT_S_CS instead of a body.
            instance.services[entry.first] = entry.second.signature;
        }
    }

    // A QUERY declaration fixes the return type; only its implementation
    // owns a logic block. This also applies to forward declarations outside INTERFACE.
    struct QueryDeclaration { VulStaticQuery signature; VulDebugLoc location; };
    unordered_map<string, QueryDeclaration> query_declarations;
    std::unordered_set<string> query_implementations;
    for (const auto &query : temp.queries) {
        VulErrorContextGuard _err{"Processing QUERY '" + query.name + "' at " +
                                 query.source_loc.file + ":" + std::to_string(query.source_loc.line)};
        auto signature = staticalizeQuery(query, local_config_lib);
        if (query.is_declaration) {
            query_declarations.emplace(query.name, QueryDeclaration{signature, query.source_loc});
            continue;
        }
        auto declaration = query_declarations.find(query.name);
        if (declaration != query_declarations.end() && declaration->second.signature.ret_type != signature.ret_type)
            throw VulException("QUERY implementation for '" + query.name + "' does not match its forward declaration: return type differs");
        query_implementations.insert(query.name);
        instance.queries[query.name] = signature;
        VulLogicBlock lb;
        lb.block_id = next_logic_block_id++;
        lb.with_priority = false;
        lb.priority = 0;
        lb.codelines = query.codelines;
        lb.codelines_debug = query.codelines_debug;
        instance.query_logic_blocks[query.name] = lb;
    }
    for (const auto &[name, declaration] : query_declarations) {
        if (!query_implementations.contains(name))
            throw VulException("QUERY forward declaration for '" + name + "' has no matching implementation at " +
                               declaration.location.file + ":" + std::to_string(declaration.location.line));
    }

}

void staticalizeTicks(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    // tick blocks
    for (size_t tb_idx = 0; tb_idx < temp.tick_blocks.size(); ++tb_idx) {
        const auto &tb = temp.tick_blocks[tb_idx];
        VulErrorContextGuard _err{"Processing tick block"};
        VulTickBlock tick_block;
        tick_block.codelines = tb;
        if (tb_idx < temp.tick_blocks_debug.size()) {
            tick_block.codelines_debug = temp.tick_blocks_debug[tb_idx];
        }
        instance.tick_blocks.push_back(std::move(tick_block));
    }

    instance.connection_config = local_config_lib;

}

} // namespace vulmodule::detail
