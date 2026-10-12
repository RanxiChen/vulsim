#include "instance_graph.h"
#include "port_shape.h"
#include "configexpr.hpp"
#include "stringop.hpp"
#include <functional>

namespace {
struct Match {
    shared_ptr<VulStaticModuleInstance> node;
    vector<ConfigRealValue> loops = {0, 0};
    vector<ConfigRealValue> wildcard_dims;
    uint32_t wildcard = 0;
};
string substitute(string expression, const vector<ConfigRealValue> &loops) {
    string out;
    for (char c : expression) {
        if (c == '$') out += std::to_string(loops[0]);
        else if (c == '?') out += std::to_string(loops[1]);
        else out += c;
    }
    return out;
}
vector<Match> matchChildren(const VulStaticModuleInstance &parent, const string &base,
        const vector<VulConnIndexExpr> &indices, const VulStaticConfigLib &config,
        const vector<ConfigRealValue> &initial, bool source) {
    vector<Match> out;
    for (const auto &child : parent.children) {
        if (child->instance_decl_name != base && child->instance_path.back() != base) continue;
        if (indices.size() != child->instance_array_indices.size()) continue;
        Match match{child, initial, {}, 0};
        // Bind source loop variables before evaluating any expressions.
        if (source) for (size_t dim = 0; dim < indices.size(); ++dim)
            if (indices[dim].kind == VulConnIndexKind::LoopVar)
                match.loops.at(indices[dim].loop_dim) = child->instance_array_indices[dim];
        bool valid = true;
        for (size_t dim = 0; dim < indices.size(); ++dim) {
            const auto &index = indices[dim];
            const auto actual = child->instance_array_indices[dim];
            if (index.kind == VulConnIndexKind::Wildcard) {
                auto size = parent.instances.at(child->instance_decl_name).array_dims.at(dim);
                match.wildcard_dims.push_back(size);
                match.wildcard = match.wildcard * size + actual;
                continue;
            }
            ConfigRealValue wanted;
            if (index.kind == VulConnIndexKind::LoopVar) wanted = match.loops.at(index.loop_dim) + index.offset;
            else wanted = calculateConstexprValue(substitute(index.expr, match.loops), config);
            if (wanted != actual) { valid = false; break; }
        }
        if (valid) out.push_back(std::move(match));
    }
    return out;
}
}

void materializeConcreteConnections(const shared_ptr<VulStaticModuleInstance> &root) {
    auto config = root->local_consts;
    config.insert(root->local_parameters.begin(), root->local_parameters.end());
    // Global constants have already been folded in connection dimensions but may occur in expressions.
    for (const auto &entry : root->connection_config) config[entry.first] = entry.second;
    for (const auto &child : root->children) materializeConcreteConnections(child);
    if (!root->child_service_ports_materialized) {
        vector<VulStaticChildServiceUse> expanded;
        std::map<string, vector<ConfigRealValue>> target_shapes;
        for (auto use : root->child_service_uses) {
            shared_ptr<VulStaticModuleInstance> child;
            for (auto node : root->children) if (node->instance_path.back() == use.instance_name) child = node;
            if (!child || !child->services.contains(use.service_name)) throw VulException("Child SERVICE not found for alias " + use.alias_name);
            const auto &service = child->services.at(use.service_name);
            uint64_t count = service.array_size;
            for (auto dim : root->child_service_shapes.at(use.alias_name)) {
                if (count > UINT32_MAX / uint64_t(dim))
                    throw VulException("USE_CHILD_SERVICE dimension product must fit uint32_t for alias " + use.alias_name);
                count *= dim;
            }
            auto [prior, inserted] = target_shapes.emplace(use.alias_name, service.port_dims);
            if (!inserted && prior->second != service.port_dims) throw VulException("USE_CHILD_SERVICE signature mismatch for alias " + use.alias_name + " at " + child->concatInstancePath("::", true));
            for (uint32_t p = 0; p < service.array_size; ++p) {
                auto slot = use;
                slot.alias_index = use.alias_index * service.array_size + p;
                slot.alias_indexed = use.alias_indexed || service.is_arrayed;
                slot.service_port_index = p;
                expanded.push_back(std::move(slot));
            }
        }
        for (auto &[name, shape] : target_shapes)
            root->child_service_shapes[name].insert(root->child_service_shapes[name].end(), shape.begin(), shape.end());
        root->child_service_uses = std::move(expanded);
        root->child_service_ports_materialized = true;
    }
    root->concrete_connections.clear();
    for (const auto &rule : root->req_connections) {
        vector<Match> sources = rule.req_instance.empty()
            ? vector<Match>{{nullptr, {0, 0}, {}, 0}}
            : matchChildren(*root, rule.req_instance_base.empty() ? rule.req_instance : rule.req_instance_base,
                            rule.req_indices, config, {0, 0}, true);
        for (const auto &source : sources) {
            vector<Match> destinations = rule.serv_instance.empty()
                ? vector<Match>{{nullptr, source.loops, {}, 0}}
                : matchChildren(*root, rule.serv_instance_base.empty() ? rule.serv_instance : rule.serv_instance_base,
                                rule.serv_indices, config, source.loops, false);
            for (const auto &destination : destinations) {
                auto conn = rule;
                conn.req_instance = source.node ? source.node->instance_path.back() : "";
                conn.serv_instance = destination.node ? destination.node->instance_path.back() : "";
                conn.req_instance_base = conn.req_instance;
                conn.serv_instance_base = conn.serv_instance;
                conn.req_indices.clear(); conn.serv_indices.clear();
                const bool inward = !source.node && !destination.wildcard_dims.empty();
                const bool outward = !destination.node && !source.wildcard_dims.empty();
                if (inward || outward) {
                    const auto &child_port = inward ? destination.node->services.at(rule.serv_name) : source.node->requests.at(rule.req_name);
                    const auto &parent_port = inward ? root->services.at(rule.req_name)
                        : (root->requests.contains(rule.serv_name) ? root->requests.at(rule.serv_name) : root->services.at(rule.serv_name));
                    auto shape = inward ? destination.wildcard_dims : source.wildcard_dims;
                    shape.insert(shape.end(), child_port.port_dims.begin(), child_port.port_dims.end());
                    if (shape != parent_port.port_dims) throw VulException("Transaction boundary shape mismatch at " + root->concatInstancePath("::", true));
                    uint32_t base = (inward ? destination.wildcard : source.wildcard) * child_port.array_size;
                    for (uint32_t p = 0; p < child_port.array_size; ++p) {
                        auto slot = conn;
                        slot.req_port_index = inward ? base + p : p;
                        slot.serv_port_index = inward ? p : base + p;
                        root->concrete_connections.push_back(std::move(slot));
                    }
                } else root->concrete_connections.push_back(std::move(conn));
            }
        }
    }
}

VulStaticModuleInstance concreteGenerationView(const VulStaticModuleInstance &module) {
    auto view = module;
    view.req_connections = module.concrete_connections;
    view.instances.clear();
    for (const auto &child : module.children) {
        VulStaticInstanceDecl decl;
        decl.name = child->instance_path.back();
        decl.module_name = child->module_name;
        view.instances.emplace(decl.name, std::move(decl));
    }
    return view;
}
