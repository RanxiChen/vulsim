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
#include "instance_graph.h"
#include "toposort.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

using namespace vulmodule::detail;

void setupUpdateSequence(shared_ptr<VulStaticModuleInstance> &top) {
    VulErrorContextGuard top_guard("Setting up update sequence for instance '" + top->simClassName() + "'");
    unordered_map<VulInstanceID, shared_ptr<VulStaticModuleInstance>> instance_id_map;
    std::deque<shared_ptr<VulStaticModuleInstance>> bfs_queue{top};
    struct ServiceNode { string port; uint32_t index; const VulLogicBlock *logic; };
    unordered_map<uint64_t, ServiceNode> service_nodes;
    unordered_map<uint64_t, string> node_names;
    std::map<std::tuple<VulInstanceID, string, uint32_t>, uint64_t> endpoint_ids;
    while (!bfs_queue.empty()) {
        auto inst = bfs_queue.front(); bfs_queue.pop_front();
        instance_id_map[inst->instance_id] = inst;
        for (const auto &child : inst->children) bfs_queue.push_back(child);
        const string path = inst->concatInstancePath("::", true);
        node_names[uint64_t(inst->instance_id) << 32] = path + ".<tick>";
        uint32_t block = 1;
        for (const auto &[name, logic] : inst->serv_logic_blocks) {
            auto decl = inst->services.find(name);
            const uint32_t count = decl == inst->services.end() ? 1 : decl->second.array_size;
            for (uint32_t idx = 0; idx < count; ++idx) {
                const uint64_t id = (uint64_t(inst->instance_id) << 32) | block++;
                endpoint_ids[{inst->instance_id, name, idx}] = id;
                service_nodes[id] = {name, idx, &logic};
                node_names[id] = path + "." + name + (count > 1 ? "[" + std::to_string(idx) + "]" : "");
            }
        }
    }
    auto debug_lb_name_by_id = [&](uint64_t id) { return node_names.at(id); };
    auto childByName = [](const shared_ptr<VulStaticModuleInstance> &parent, const string &name) {
        for (const auto &child : parent->children) if (child->instance_path.back() == name) return child;
        throw VulException("Concrete child instance not found: " + name);
    };
    std::function<vector<uint64_t>(shared_ptr<VulStaticModuleInstance>, string, uint32_t, bool, std::set<string>)> resolve;
    resolve = [&](auto inst, string port, uint32_t index, bool service, std::set<string> path) -> vector<uint64_t> {
        const string key = std::to_string(inst->instance_id) + ":" + port + ":" + std::to_string(index) + (service ? ":S" : ":R");
        if (!path.insert(key).second) throw VulException("Cyclic transaction forwarding at " + inst->concatInstancePath("::", true) + "." + port);
        const auto &ports = service ? inst->services : inst->requests;
        auto decl = ports.find(port);
        if (decl == ports.end() || index >= decl->second.array_size)
            throw VulException("Invalid transaction endpoint: " + inst->concatInstancePath("::", true) + "." + port + "[" + std::to_string(index) + "]");
        if (service) {
            auto found = endpoint_ids.find({inst->instance_id, port, index});
            if (found != endpoint_ids.end()) return {found->second};
        }
        auto scope = service ? inst : inst->parent;
        if (!scope) throw VulException("Unconnected root request: " + port);
        vector<uint64_t> out;
        for (const auto &conn : scope->concrete_connections) {
            if (conn.req_instance != (service ? "" : inst->instance_path.back()) || conn.req_name != port) continue;
            if (conn.req_port_index >= 0 && uint32_t(conn.req_port_index) != index) continue;
            auto destination = conn.serv_instance.empty() ? scope : childByName(scope, conn.serv_instance);
            bool next_service = !conn.serv_instance.empty() || !scope->requests.contains(conn.serv_name);
            const auto &dstports = next_service ? destination->services : destination->requests;
            auto dstdecl = dstports.find(conn.serv_name);
            if (dstdecl == dstports.end()) throw VulException("Missing destination transaction: " + conn.serv_name);
            const uint32_t dstindex = conn.serv_port_index >= 0 ? conn.serv_port_index : (dstdecl->second.is_arrayed ? index : 0);
            auto targets = resolve(destination, conn.serv_name, dstindex, next_service, path);
            out.insert(out.end(), targets.begin(), targets.end());
        }
        if (out.empty()) throw VulException("No connected service found for " + inst->concatInstancePath("::", true) + "." + port);
        return out;
    };
    unordered_map<uint64_t, vector<uint64_t>> logic_block_call_graph;
    auto scan = [&](const shared_ptr<VulStaticModuleInstance> &inst, uint64_t source, const vector<string> &lines, uint32_t slot, const VulDebugLocs &locations) {
        auto config = inst->connection_config;
        config["IDX"] = slot;
        vector<ConfigRealValue> service_shape;
        if (source & 0xFFFFFFFF) service_shape = inst->services.at(service_nodes.at(source).port).port_dims;
        const string text = activeConstexprCode(lowerPortIndexDims(joinCode(lines), service_shape, config), config);
        vector<string> names;
        for (const auto &[name, _] : inst->requests) names.push_back(name);
        for (const auto &use : inst->child_service_uses)
            if (std::find(names.begin(), names.end(), use.alias_name) == names.end()) names.push_back(use.alias_name);
        for (const auto &name : names) {
            for (const auto &[offset, expression] : transactionCallIndices(text, name)) {
                LogicBlockCall call;
                call.port = name;
                call.index_expression = expression;
                const size_t line_index = std::count(text.begin(), text.begin() + offset, '\n');
                if (line_index < locations.size()) call.source_location = locations[line_index];
                const string location = call.source_location.valid() ? " at " + call.source_location.file + ":" + std::to_string(call.source_location.line) : "";
                VulErrorContextGuard call_guard("Analyzing transaction call '" + name + "' in " + inst->concatInstancePath("::", true) + location);
                auto dims = inst->requests.contains(name) ? inst->requests.at(name).port_dims : inst->child_service_shapes.at(name);
                vector<uint64_t> targets;
                for (auto idx : portCallSlots(dims, call.index_expression, config)) {
                    if (inst->requests.contains(name)) {
                        auto result = resolve(inst, name, idx, false, {});
                        targets.insert(targets.end(), result.begin(), result.end());
                    } else {
                        bool found = false;
                        for (const auto &use : inst->child_service_uses) {
                            if (use.alias_name != name || (use.alias_indexed && use.alias_index != idx)) continue;
                            auto child = childByName(inst, use.instance_name);
                            auto result = resolve(child, use.service_name, use.service_port_index, true, {});
                            targets.insert(targets.end(), result.begin(), result.end()); found = true;
                        }
                        if (!found) throw VulException("Child service alias index out of range: " + name);
                    }
                }
                auto &edges = logic_block_call_graph[source];
                edges.insert(edges.end(), targets.begin(), targets.end());
            }
        }
    };
    for (const auto &[id, inst] : instance_id_map) {
        for (const auto &tick : inst->tick_blocks) {
            const uint64_t source = uint64_t(id) << 32;
            scan(inst, source, tick.codelines, 0, tick.codelines_debug);
            // TestMain's externally callable services have synthetic callers.
            if (inst->module_name == "TestMain") for (const auto &call : tick.call_requests) {
                for (uint32_t idx = 0; idx < inst->requests.at(call.port).array_size; ++idx) {
                    auto result = resolve(inst, call.port, idx, false, {});
                    logic_block_call_graph[source].insert(logic_block_call_graph[source].end(), result.begin(), result.end());
                }
            }
        }
    }
    for (const auto &[id, node] : service_nodes) scan(instance_id_map.at(id >> 32), id, node.logic->codelines, node.index, node.logic->codelines_debug);
    unordered_map<VulInstanceID, vector<uint64_t>> instance_tick_to_lb_call_graph;
    unordered_map<uint64_t, VulInstanceID> logic_block_id_to_instance_id;
    for (const auto &[instid, inst] : instance_id_map) {
        std::set<uint64_t> active, visited;
        vector<uint64_t> path;
        std::function<void(uint64_t)> visit = [&](uint64_t id) {
            path.push_back(id);
            if (active.contains(id) || visited.contains(id)) {
                string diagnostic;
                for (auto p : path) { if (!diagnostic.empty()) diagnostic += " -> "; diagnostic += debug_lb_name_by_id(p); }
                throw VulException(string(active.contains(id) ? "Cyclic call" : "Repeated call") + " detected in logic block call graph: " + diagnostic);
            }
            active.insert(id); visited.insert(id);
            if ((id & 0xFFFFFFFF) != 0) {
                instance_tick_to_lb_call_graph[instid].push_back(id);
                auto [previous, inserted] = logic_block_id_to_instance_id.emplace(id, instid);
                if (!inserted && previous->second != instid)
                    throw VulException("Logic block '" + debug_lb_name_by_id(id) + "' is called by multiple instances: '" + debug_lb_name_by_id(uint64_t(previous->second) << 32) + "' and '" + debug_lb_name_by_id(uint64_t(instid) << 32) + "'.");
            }
            for (auto target : logic_block_call_graph[id]) visit(target);
            active.erase(id); path.pop_back();
        };
        visit(uint64_t(instid) << 32);
    }

    VulErrorContextGuard order_guard("Determining instance update order");

    unordered_map<VulInstanceID, unordered_set<VulInstanceID>> instance_order_graph;
    bfs_queue.clear();
    bfs_queue.push_back(top);
    while (!bfs_queue.empty()) {
        auto cur_inst = bfs_queue.front();
        bfs_queue.pop_front();
        VulInstanceID cur_inst_id = cur_inst->instance_id;
        for (const auto &child : cur_inst->children) bfs_queue.push_back(child);
        map<int32_t, vector<VulInstanceID>> priority_to_callee_instances;
        priority_to_callee_instances[0].push_back(cur_inst_id); // tick block has default priority 0

        VulErrorContextGuard inst_guard("Processing instance '" + cur_inst->concatInstancePath("::", true) + "' (IID: " + std::to_string(cur_inst_id) + ") for update order");

        for (const auto &[lb_id, node] : service_nodes) {
            if ((lb_id >> 32) != cur_inst_id) continue;
            const auto &serv_lb = *node.logic;
            if (!serv_lb.with_priority) continue;
            auto caller = logic_block_id_to_instance_id.find(lb_id);
            if (caller == logic_block_id_to_instance_id.end()) continue;
            const auto caller_id = caller->second;
            if (caller_id == cur_inst_id)
                throw VulException("Logic block '" + debug_lb_name_by_id(lb_id) + "' is called by its own tick block.");
            priority_to_callee_instances[serv_lb.priority].push_back(caller_id);
        }

        unordered_set<VulInstanceID> higher_priority_instance_set;
        vector<VulInstanceID> higher_priority_instances;
        for (auto prio_it = priority_to_callee_instances.rbegin(); prio_it != priority_to_callee_instances.rend(); ++prio_it) {
            auto &same_priority_instances = prio_it->second;
            if (same_priority_instances.empty()) {
                continue;
            }
            std::sort(same_priority_instances.begin(), same_priority_instances.end());
            same_priority_instances.erase(std::unique(same_priority_instances.begin(), same_priority_instances.end()), same_priority_instances.end());

            if (!higher_priority_instances.empty()) {
                for (VulInstanceID from_inst_id : higher_priority_instances) {
                    auto &out_edges = instance_order_graph[from_inst_id];
                    for (VulInstanceID to_inst_id : same_priority_instances) {
                        if (from_inst_id != to_inst_id) {
                            out_edges.insert(to_inst_id);
                        }
                    }
                }
            }

            for (VulInstanceID inst_id : same_priority_instances) {
                if (higher_priority_instance_set.insert(inst_id).second) {
                    higher_priority_instances.push_back(inst_id);
                }
            }
        }
    }

    unordered_map<VulInstanceID, unordered_map<VulInstanceID, unordered_set<VulInstanceID>>> child_instance_order_graph; // instance_id -> child_instance_id -> set of child_instance_id that should be updated after this child_instance_id
    for (const auto &entry : instance_order_graph) {
        const auto &former_inst_id = entry.first;
        const auto &latter_inst_ids = entry.second;
        auto former_inst_iter = instance_id_map.find(former_inst_id);
        if (former_inst_iter == instance_id_map.end()) {
            throw VulException("Instance ID " + std::to_string(former_inst_id) + " not found in instance_id_map");
        }
        shared_ptr<VulStaticModuleInstance> former_inst_ptr = former_inst_iter->second;
        for (const auto &latter_inst_id : latter_inst_ids) {
            auto latter_inst_iter = instance_id_map.find(latter_inst_id);
            if (latter_inst_iter == instance_id_map.end()) {
                throw VulException("Instance ID " + std::to_string(latter_inst_id) + " not found in instance_id_map");
            }
            shared_ptr<VulStaticModuleInstance> latter_inst_ptr = latter_inst_iter->second;

            size_t former_depth = former_inst_ptr->instance_path.size();
            size_t latter_depth = latter_inst_ptr->instance_path.size();
            shared_ptr<VulStaticModuleInstance> former_ancestor = former_inst_ptr;
            shared_ptr<VulStaticModuleInstance> latter_ancestor = latter_inst_ptr;
            VulInstanceID former_last_id = former_inst_id;
            VulInstanceID latter_last_id = latter_inst_id;

            while (former_depth > latter_depth) {
                former_last_id = former_ancestor->instance_id;
                former_ancestor = former_ancestor->parent;
                --former_depth;
            }
            while (latter_depth > former_depth) {
                latter_last_id = latter_ancestor->instance_id;
                latter_ancestor = latter_ancestor->parent;
                --latter_depth;
            }
            while (former_ancestor.get() != latter_ancestor.get()) {
                if (former_ancestor == nullptr || latter_ancestor == nullptr) {
                    throw VulException("Cannot find common ancestor for instance IDs " + std::to_string(former_inst_id) + " and " + std::to_string(latter_inst_id));
                }
                former_last_id = former_ancestor->instance_id;
                latter_last_id = latter_ancestor->instance_id;
                former_ancestor = former_ancestor->parent;
                latter_ancestor = latter_ancestor->parent;
            }
            if (former_ancestor == nullptr) {
                throw VulException("Cannot find common ancestor for instance IDs " + std::to_string(former_inst_id) + " and " + std::to_string(latter_inst_id));
            }

            shared_ptr<VulStaticModuleInstance> common_ancestor = former_ancestor;
            // An endpoint equal to the LCA denotes that ancestor's local tick.
            // Otherwise it denotes the direct child subtree containing the endpoint.
            auto update_node = [&](shared_ptr<VulStaticModuleInstance> endpoint) {
                if (endpoint == common_ancestor) return endpoint->instance_id;
                while (endpoint->parent != common_ancestor) endpoint = endpoint->parent;
                return endpoint->instance_id;
            };
            former_last_id = update_node(former_inst_ptr);
            latter_last_id = update_node(latter_inst_ptr);
            if (former_last_id != latter_last_id) {
                child_instance_order_graph[common_ancestor->instance_id][former_last_id].insert(latter_last_id);
            }
        }
    }

    bfs_queue.clear();
    bfs_queue.push_back(top);
    while (!bfs_queue.empty()) {
        auto cur_inst = bfs_queue.front();
        bfs_queue.pop_front();

        const VulInstanceID cur_inst_id = cur_inst->instance_id;
        vector<VulInstanceID> update_nodes;
        update_nodes.reserve(cur_inst->children.size() + 1);
        update_nodes.push_back(cur_inst_id); // self ID represents local tick
        for (const auto &child : cur_inst->children) {
            update_nodes.push_back(child->instance_id);
            bfs_queue.push_back(child);
        }

        unordered_map<VulInstanceID, int32_t> indegree;
        indegree.reserve(update_nodes.size() * 2);
        for (VulInstanceID node_id : update_nodes) {
            indegree.emplace(node_id, 0);
        }

        unordered_map<VulInstanceID, vector<VulInstanceID>> adj;
        auto order_it = child_instance_order_graph.find(cur_inst_id);
        if (order_it != child_instance_order_graph.end()) {
            adj.reserve(order_it->second.size());
            for (const auto &from_entry : order_it->second) {
                VulInstanceID from_id = from_entry.first;
                if (indegree.find(from_id) == indegree.end()) {
                    continue;
                }
                auto &out_edges = adj[from_id];
                out_edges.reserve(from_entry.second.size());
                for (VulInstanceID to_id : from_entry.second) {
                    auto indeg_it = indegree.find(to_id);
                    if (indeg_it == indegree.end() || to_id == from_id) {
                        continue;
                    }
                    out_edges.push_back(to_id);
                    indeg_it->second += 1;
                }
            }
        }

        std::priority_queue<VulInstanceID, vector<VulInstanceID>, std::greater<VulInstanceID>> ready;
        for (const auto &entry : indegree) {
            if (entry.second == 0) {
                ready.push(entry.first);
            }
        }

        cur_inst->update_seq.clear();
        cur_inst->update_seq.reserve(update_nodes.size());

        while (!ready.empty()) {
            VulInstanceID from_id = ready.top();
            ready.pop();
            cur_inst->update_seq.push_back(from_id);

            auto adj_it = adj.find(from_id);
            if (adj_it == adj.end()) {
                continue;
            }
            for (VulInstanceID to_id : adj_it->second) {
                auto indeg_it = indegree.find(to_id);
                if (indeg_it == indegree.end()) {
                    continue;
                }
                indeg_it->second -= 1;
                if (indeg_it->second == 0) {
                    ready.push(to_id);
                }
            }
        }

        if (cur_inst->update_seq.size() != update_nodes.size()) {
            std::set<VulInstanceID> blocked;
            string diagnostic = "Cyclic update constraints found in instance update order graph at " + cur_inst->concatInstancePath("::", true) + ":";
            for (auto id : update_nodes) if (indegree.at(id) > 0) {
                blocked.insert(id);
                diagnostic += " " + instance_id_map.at(id)->concatInstancePath("::", true);
            }
            vector<string> calls;
            for (const auto &[id, node] : service_nodes) {
                auto caller = logic_block_id_to_instance_id.find(id);
                if (!node.logic->with_priority || caller == logic_block_id_to_instance_id.end()) continue;
                auto projected = instance_id_map.at(caller->second);
                while (projected && projected != cur_inst && projected->parent != cur_inst) projected = projected->parent;
                if (!projected || !blocked.contains(projected->instance_id)) continue;
                calls.push_back(debug_lb_name_by_id(uint64_t(caller->second) << 32) + " -> " + debug_lb_name_by_id(id)
                    + " (priority=" + std::to_string(node.logic->priority) + ")");
            }
            std::sort(calls.begin(), calls.end());
            for (const auto &call : calls) diagnostic += "\n  " + call;
            throw VulException(diagnostic);
        }
    }

}
