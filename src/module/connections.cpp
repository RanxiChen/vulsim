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
#include "stringop.hpp"
#include "instance_graph.h"
#include "port_shape.h"
#include <cctype>
#include <optional>

namespace vulmodule::detail {
namespace {
struct ParsedInstanceExpr {
    string base_name;
    vector<string> index_exprs;
};

static bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static ParsedInstanceExpr parseInstanceExpr(const string &expr_raw) {
    const string expr = stringop::trim(expr_raw);
    if (expr.empty()) {
        return {};
    }

    size_t pos = 0;
    if (!isIdentStart(expr[pos])) {
        throw VulException("Invalid instance expression: '" + expr + "'");
    }
    while (pos < expr.size() && isIdentChar(expr[pos])) {
        ++pos;
    }

    ParsedInstanceExpr out;
    out.base_name = expr.substr(0, pos);

    while (pos < expr.size()) {
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        if (pos >= expr.size()) {
            break;
        }
        if (expr[pos] != '[') {
            throw VulException("Invalid instance expression: '" + expr + "'");
        }
        const size_t begin = ++pos;
        int depth = 1;
        bool in_string = false;
        bool in_char = false;
        bool escape = false;
        for (; pos < expr.size(); ++pos) {
            const char c = expr[pos];
            if (in_string || in_char) {
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (in_string && c == '"') {
                    in_string = false;
                } else if (in_char && c == '\'') {
                    in_char = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == '\'') {
                in_char = true;
                continue;
            }
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (pos >= expr.size() || expr[pos] != ']') {
            throw VulException("Unmatched '[' in instance expression: '" + expr + "'");
        }
        out.index_exprs.push_back(stringop::trim(expr.substr(begin, pos - begin)));
        ++pos;
    }
    return out;
}

static string arrayInstanceName(const string &base_name, const vector<ConfigRealValue> &indices) {
    return concreteInstanceName(base_name, indices);
}

static string replaceLoopVars(const string &expr, const vector<ConfigRealValue> &loop_vars) {
    string out;
    out.reserve(expr.size() * 2);
    for (char c : expr) {
        if (c == '$') {
            out += "__v0";
        } else if (c == '?') {
            out += "__v1";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static ConfigRealValue evalExprWithLoopVars(
    const string &expr,
    const VulStaticConfigLib &config_lib,
    const vector<ConfigRealValue> &loop_vars
) {
    VulStaticConfigLib eval_cfg = config_lib;
    if (loop_vars.size() > 0) {
        eval_cfg["__v0"] = loop_vars[0];
    }
    if (loop_vars.size() > 1) {
        eval_cfg["__v1"] = loop_vars[1];
    }
    return calculateConstexprValue(replaceLoopVars(expr, loop_vars), eval_cfg);
}

static const VulStaticInstanceDecl &requireInstanceDecl(
    const unordered_map<InstanceName, VulStaticInstanceDecl> &instances,
    const string &name,
    const string &ctx
) {
    auto it = instances.find(name);
    if (it == instances.end()) {
        throw VulException("Instance '" + name + "' not found while " + ctx);
    }
    return it->second;
}

template<typename Fn>
static void forEachIndexTuple(const vector<ConfigRealValue> &dims, Fn fn) {
    vector<ConfigRealValue> indices(dims.size(), 0);
    std::function<void(size_t)> dfs = [&](size_t dim) {
        if (dim == dims.size()) {
            fn(indices);
            return;
        }
        for (ConfigRealValue idx = 0; idx < dims[dim]; ++idx) {
            indices[dim] = idx;
            dfs(dim + 1);
        }
    };
    if (dims.empty()) {
        fn(indices);
    } else {
        dfs(0);
    }
}

static bool hasLoopVar(const string &expr) {
    return expr.find('$') != string::npos || expr.find('?') != string::npos;
}

static bool hasWildcard(const string &expr) {
    return expr.find('*') != string::npos;
}

static VulConnIndexExpr parseConnIndexExpr(const string &expr_raw) {
    const string expr = stringop::trim(expr_raw);
    if (expr.empty()) {
        throw VulException("Empty array index expression");
    }
    if (expr == "*") {
        VulConnIndexExpr out;
        out.kind = VulConnIndexKind::Wildcard;
        out.expr = expr;
        return out;
    }

    auto parse_loop_var = [&](char symbol, int32_t loop_dim) -> std::optional<VulConnIndexExpr> {
        const size_t pos = expr.find(symbol);
        if (pos == string::npos) return std::nullopt;
        if (expr.find(symbol, pos + 1) != string::npos) {
            return std::nullopt;
        }
        if (expr.find(symbol == '$' ? '?' : '$') != string::npos) {
            return std::nullopt;
        }
        string lhs = stringop::trim(expr.substr(0, pos));
        string rhs = stringop::trim(expr.substr(pos + 1));
        int64_t offset = 0;
        if (!lhs.empty()) {
            return std::nullopt;
        }
        if (!rhs.empty()) {
            if (rhs[0] == '+') {
                offset = std::stoll(stringop::trim(rhs.substr(1)));
            } else if (rhs[0] == '-') {
                offset = -std::stoll(stringop::trim(rhs.substr(1)));
            } else {
                return std::nullopt;
            }
        }
        VulConnIndexExpr out;
        out.kind = VulConnIndexKind::LoopVar;
        out.loop_dim = loop_dim;
        out.offset = offset;
        out.expr = expr;
        return out;
    };

    if (auto parsed = parse_loop_var('$', 0)) return *parsed;
    if (auto parsed = parse_loop_var('?', 1)) return *parsed;

    if (hasLoopVar(expr)) {
        VulConnIndexExpr out;
        out.kind = VulConnIndexKind::GeneralExpr;
        out.expr = expr;
        return out;
    }

    VulConnIndexExpr out;
    out.kind = VulConnIndexKind::ConstantExpr;
    out.expr = expr;
    return out;
}

static vector<VulConnIndexExpr> parseConnIndices(const ParsedInstanceExpr &parsed) {
    vector<VulConnIndexExpr> out;
    out.reserve(parsed.index_exprs.size());
    for (const auto &expr : parsed.index_exprs) {
        out.push_back(parseConnIndexExpr(expr));
    }
    return out;
}

static bool tryResolveConcreteInstance(
    const ParsedInstanceExpr &parsed,
    const VulStaticInstanceDecl &decl,
    const VulStaticConfigLib &config_lib,
    const vector<ConfigRealValue> &loop_vars,
    string &concrete_name
) {
    if (parsed.index_exprs.size() != decl.array_dims.size()) {
        throw VulException("Dimension mismatch when resolving instance '" + parsed.base_name + "'");
    }
    vector<ConfigRealValue> indices;
    indices.reserve(parsed.index_exprs.size());
    for (size_t i = 0; i < parsed.index_exprs.size(); ++i) {
        const string &expr = parsed.index_exprs[i];
        if (hasWildcard(expr)) {
            throw VulException("Wildcard '*' is only allowed in module boundary connections");
        }
        ConfigRealValue idx = evalExprWithLoopVars(expr, config_lib, loop_vars);
        if (idx < 0 || idx >= decl.array_dims[i]) {
            return false;
        }
        indices.push_back(idx);
    }
    concrete_name = decl.isArrayed() ? arrayInstanceName(parsed.base_name, indices) : parsed.base_name;
    return true;
}


} // namespace

void staticalizeConnections(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    // req-serv connections
    instance.req_connections.clear();
    for (const auto &temp_conn : temp.req_connections) {
        VulReqServConnection conn = temp_conn;
        const ParsedInstanceExpr req_expr = parseInstanceExpr(temp_conn.req_instance);
        const ParsedInstanceExpr serv_expr = parseInstanceExpr(temp_conn.serv_instance);
        const bool req_is_top = temp_conn.req_instance.empty();
        const bool serv_is_top = temp_conn.serv_instance.empty();

        conn.req_instance_base = req_expr.base_name;
        conn.req_indices = parseConnIndices(req_expr);
        conn.serv_instance_base = serv_expr.base_name;
        conn.serv_indices = parseConnIndices(serv_expr);

        auto validate_child_ref = [&](const ParsedInstanceExpr &expr, const vector<VulConnIndexExpr> &indices, const string &ctx, bool allow_wildcard) {
            if (expr.base_name.empty()) return;
            const auto &decl = requireInstanceDecl(instance.instances, expr.base_name, ctx);
            if (expr.index_exprs.size() != decl.array_dims.size()) {
                throw VulException("Dimension mismatch for array instance '" + expr.base_name + "'");
            }
            size_t wildcard_count = 0;
            for (const auto &idx : indices) {
                if (idx.kind == VulConnIndexKind::Wildcard) {
                    ++wildcard_count;
                    if (!allow_wildcard) {
                        throw VulException("Wildcard '*' is only allowed in module boundary connections");
                    }
                }
            }
        };

        if (!req_is_top) {
            validate_child_ref(req_expr, conn.req_indices, "processing request side connection", serv_is_top);
        }
        if (!serv_is_top) {
            validate_child_ref(serv_expr, conn.serv_indices, "processing service side connection", req_is_top);
        }

        if (!req_is_top && !serv_is_top) {
            const auto &req_decl = requireInstanceDecl(instance.instances, req_expr.base_name, "checking request side");
            const auto &serv_decl = requireInstanceDecl(instance.instances, serv_expr.base_name, "checking service side");
            vector<ConfigRealValue> loop_dims(2, -1);
            vector<int32_t> source_loop_dim_pos(2, -1);
            for (size_t i = 0; i < conn.req_indices.size(); ++i) {
                const auto &idx = conn.req_indices[i];
                if (idx.kind == VulConnIndexKind::GeneralExpr) {
                    throw VulException(
                        "Loop variables on source instance '" + req_expr.base_name +
                        "' must appear alone in one dimension, such as [$] or [?]"
                    );
                }
                if (idx.kind != VulConnIndexKind::LoopVar) continue;
                if (idx.offset != 0) {
                    throw VulException(
                        "Loop variables on source instance '" + req_expr.base_name +
                        "' cannot use offsets; use [$] or [?] instead of '" + idx.expr + "'"
                    );
                }
                const int32_t loop_dim = idx.loop_dim;
                if (loop_dim < 0 || static_cast<size_t>(loop_dim) >= loop_dims.size()) {
                    throw VulException("Loop variable index out of range in '" + req_expr.base_name + "'");
                }
                if (source_loop_dim_pos[loop_dim] >= 0) {
                    throw VulException("Loop variable appears multiple times on source instance '" + req_expr.base_name + "'");
                }
                source_loop_dim_pos[loop_dim] = static_cast<int32_t>(i);
                loop_dims[loop_dim] = req_decl.array_dims[i];
            }

            auto validate_loop_var_uses = [&](const vector<VulConnIndexExpr> &indices, const string &instance_name) {
                for (const auto &idx : indices) {
                    if (idx.kind == VulConnIndexKind::ConstantExpr || idx.kind == VulConnIndexKind::Wildcard) {
                        continue;
                    }
                    if (idx.kind == VulConnIndexKind::LoopVar) {
                        if (idx.loop_dim < 0 || static_cast<size_t>(idx.loop_dim) >= loop_dims.size() || source_loop_dim_pos[idx.loop_dim] < 0) {
                            throw VulException("Loop variable used in destination instance '" + instance_name + "' is not defined on the source side");
                        }
                        continue;
                    }
                    if (idx.kind == VulConnIndexKind::GeneralExpr) {
                        if (idx.expr.find('$') != string::npos && source_loop_dim_pos[0] < 0) {
                            throw VulException("Destination expression '" + idx.expr + "' uses '$' without a matching source-side [$]");
                        }
                        if (idx.expr.find('?') != string::npos && source_loop_dim_pos[1] < 0) {
                            throw VulException("Destination expression '" + idx.expr + "' uses '?' without a matching source-side [?]");
                        }
                    }
                }
            };
            validate_loop_var_uses(conn.serv_indices, serv_expr.base_name);

            vector<ConfigRealValue> loop_vars(2, 0);
            std::function<void(size_t)> validate_dest_eval = [&](size_t var_id) {
                if (var_id == loop_dims.size()) {
                    for (size_t dim = 0; dim < conn.serv_indices.size(); ++dim) {
                        const auto &idx = conn.serv_indices[dim];
                        if (idx.kind == VulConnIndexKind::Wildcard) {
                            throw VulException("Wildcard '*' is only allowed in module boundary connections");
                        }
                        if (idx.kind == VulConnIndexKind::ConstantExpr || idx.kind == VulConnIndexKind::GeneralExpr) {
                            (void)evalExprWithLoopVars(idx.expr, local_config_lib, loop_vars);
                        }
                    }
                    return;
                }
                if (source_loop_dim_pos[var_id] < 0) {
                    validate_dest_eval(var_id + 1);
                    return;
                }
                for (ConfigRealValue idx = 0; idx < loop_dims[var_id]; ++idx) {
                    loop_vars[var_id] = idx;
                    validate_dest_eval(var_id + 1);
                }
            };
            validate_dest_eval(0);
        } else {
            const ParsedInstanceExpr &child_expr = req_is_top ? serv_expr : req_expr;
            const vector<VulConnIndexExpr> &child_indices = req_is_top ? conn.serv_indices : conn.req_indices;
            if (!child_expr.base_name.empty()) {
                const auto &child_decl = requireInstanceDecl(instance.instances, child_expr.base_name, "checking module boundary connection");
                if (child_decl.isArrayed() && child_indices.empty()) {
                    throw VulException("Array child instance '" + child_expr.base_name + "' requires explicit indices in connection");
                }
                for (const auto &idx : child_indices) {
                    if (idx.kind == VulConnIndexKind::LoopVar || idx.kind == VulConnIndexKind::GeneralExpr) {
                        throw VulException("Loop variables are only allowed in internal array connection rules");
                    }
                }
            }
        }

        instance.req_connections.push_back(std::move(conn));
    }

    // child service uses
    auto check_child_alias_dims = [&](const string &macro, const string &alias,
                                      const vector<string> &dims, bool explicit_dims,
                                      const vector<ConfigRealValue> &shape) {
        if (!explicit_dims) return;
        vector<ConfigRealValue> declared;
        for (const auto &dim : dims) {
            auto value = calculateConstexprValue(dim, local_config_lib);
            if (value <= 0) throw VulException(macro + " dims for alias '" + alias + "' must be positive");
            declared.push_back(value);
        }
        if (declared != shape)
            throw VulException(macro + " dims for alias '" + alias + "' do not match wildcard shape");
    };

    instance.child_service_uses.clear();
    instance.child_service_shapes.clear();
    instance.child_service_ports_materialized = false;
    for (const auto &temp_use : temp.child_service_uses) {
        const auto expr = parseInstanceExpr(temp_use.instance_expr);
        const auto &decl = requireInstanceDecl(instance.instances, expr.base_name, "processing USE_CHILD_SERVICE");
        if (expr.index_exprs.size() != decl.array_dims.size())
            throw VulException("Array child instance requires explicit indices in USE_CHILD_SERVICE");
        vector<ConfigRealValue> shape;
        for (size_t d = 0; d < expr.index_exprs.size(); ++d)
            if (stringop::trim(expr.index_exprs[d]) == "*") shape.push_back(decl.array_dims[d]);
        instance.child_service_shapes[temp_use.alias_name] = shape;
        ConfigRealValue count = 1; for (auto dim : shape) count *= dim;
        check_child_alias_dims("USE_CHILD_SERVICE", temp_use.alias_name,
                               temp_use.dims, temp_use.has_dims, shape);
        for (uint32_t i = 0; i < count; ++i) {
            const auto wild = portCoordinates(i, shape);
            vector<ConfigRealValue> coords; size_t w = 0;
            for (size_t d = 0; d < expr.index_exprs.size(); ++d) {
                auto value = stringop::trim(expr.index_exprs[d]) == "*" ? wild[w++] : calculateConstexprValue(expr.index_exprs[d], local_config_lib);
                if (value < 0 || value >= decl.array_dims[d]) throw VulException("Indexed child instance out of range in USE_CHILD_SERVICE");
                coords.push_back(value);
            }
            VulStaticChildServiceUse use;
            use.alias_name = temp_use.alias_name; use.instance_name = arrayInstanceName(expr.base_name, coords);
            use.service_name = temp_use.service_name; use.alias_indexed = !shape.empty(); use.alias_index = i;
            instance.child_service_uses.push_back(std::move(use));
        }
    }

    // Wildcard child queries aggregate values. Their shape belongs to the
    // returned std::array, never to function template parameters.
    instance.child_query_uses.clear();
    for (const auto &temp_use : temp.child_query_uses) {
        const auto expr = parseInstanceExpr(temp_use.instance_expr);
        const auto &decl = requireInstanceDecl(instance.instances, expr.base_name, "processing USE_CHILD_QUERY");
        if (expr.index_exprs.size() != decl.array_dims.size())
            throw VulException("Array child instance requires explicit indices in USE_CHILD_QUERY");
        vector<ConfigRealValue> shape;
        uint64_t count = 1;
        for (size_t d = 0; d < expr.index_exprs.size(); ++d) {
            if (stringop::trim(expr.index_exprs[d]) == "*") {
                shape.push_back(decl.array_dims[d]);
                if (uint64_t(decl.array_dims[d]) > UINT32_MAX / count)
                    throw VulException("USE_CHILD_QUERY wildcard shape product must fit uint32_t");
                count *= decl.array_dims[d];
            }
        }
        check_child_alias_dims("USE_CHILD_QUERY", temp_use.alias_name, temp_use.dims, temp_use.has_dims, shape);
        const auto type = parseTypeSignature(temp_use.ret_type, local_config_lib);
        for (uint32_t i = 0; i < count; ++i) {
            auto wildcard = portCoordinates(i, shape);
            vector<ConfigRealValue> coordinates;
            size_t w = 0;
            for (size_t d = 0; d < expr.index_exprs.size(); ++d) {
                const auto value = stringop::trim(expr.index_exprs[d]) == "*" ? wildcard[w++]
                    : calculateConstexprValue(expr.index_exprs[d], local_config_lib);
                if (value < 0 || value >= decl.array_dims[d])
                    throw VulException("Indexed child instance out of range in USE_CHILD_QUERY");
                coordinates.push_back(value);
            }
            VulStaticChildQueryUse use;
            use.alias_name = temp_use.alias_name;
            use.instance_name = arrayInstanceName(expr.base_name, coordinates);
            use.query_name = temp_use.query_name;
            use.ret_type = type;
            use.return_dims = shape;
            use.alias_index = i;
            instance.child_query_uses.push_back(std::move(use));
        }
    }

}

} // namespace vulmodule::detail
