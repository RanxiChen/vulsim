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

#include "declaration_args.hpp"

#include "common.hpp"
#include "../stringop.hpp"
#include <algorithm>
#include <map>
#include <set>

using namespace cppparse;
using stringop::trim;

namespace {
struct Spec {
    vector<string> keys;
    size_t prefix;
    vector<string> required;
    bool business = false;
    bool instance = false;
};

const std::map<string, Spec> specs = {
    {"CONFIG", {{"name", "value"}, 2, {"name", "value"}}},
    {"PARAMETER", {{"name", "value"}, 2, {"name", "value"}}},
    {"STRUCT", {{"name"}, 1, {"name"}}},
    {"ENUM", {{"name"}, 1, {"name"}}},
    {"ALIAS", {{"name", "type", "dims"}, 2, {"name", "type"}}},
    {"PIPELINE_ATTR", {{"name", "latency"}, 2, {"name", "latency"}}},
    {"IREG", {{"name", "type", "ports"}, 2, {"name", "type"}}},
    {"OREG", {{"name", "type"}, 2, {"name", "type"}}},
    {"IVALID", {{"name", "ports"}, 1, {"name"}}},
    {"OVALID", {{"name"}, 1, {"name"}}},
    {"REGISTER", {{"name", "type", "ports", "dims"}, 2, {"name", "type"}}},
    {"WIRE", {{"name", "type"}, 2, {"name", "type"}}},
    {"REQUEST", {{"name", "handshake", "dims"}, 1, {"name"}, true}},
    {"SERVICE", {{"name", "handshake", "dims", "priority", "ready"}, 1, {"name"}, true}},
    {"QUERY", {{"name", "type"}, 2, {"name", "type"}}},
    {"CHILD_INSTANCE", {{"module", "name", "dims"}, 2, {"module", "name"}, false, true}},
    {"USE_CHILD_SERVICE", {{"instance", "service", "alias", "dims"}, 3, {"instance", "service", "alias"}, true}},
    {"USE_CHILD_QUERY", {{"instance", "query", "alias", "type", "dims"}, 4, {"instance", "query", "alias", "type"}}},
    {"BRAM", {{"name", "type", "size", "read_ports", "write_ports", "mode"}, 3, {"name", "type", "size"}}},
    {"ROM", {{"name", "data_width", "size", "read_ports", "init"}, 3, {"name", "data_width", "size", "init"}}},
    {"QUEUE", {{"name", "type", "depth", "enq_width", "deq_width"}, 3, {"name", "type", "depth"}}},
    {"CONNECT_CR_CS", {{"srcchild", "srcreq", "dstchild", "dstserv"}, 4, {"srcchild", "srcreq", "dstchild", "dstserv"}}},
    {"CONNECT_CR_S", {{"srcchild", "srcreq", "dstserv"}, 3, {"srcchild", "srcreq", "dstserv"}}},
    {"CONNECT_CR_R", {{"srcchild", "srcreq", "dstreq"}, 3, {"srcchild", "srcreq", "dstreq"}}},
    {"CONNECT_S_CS", {{"srcserv", "dstchild", "dstserv"}, 3, {"srcserv", "dstchild", "dstserv"}}},
    {"VERSION", {{"name"}, 1, {"name"}}},
    {"USE_VERSION", {{"name"}, 1, {"name"}}},
    {"TOP", {{"path"}, 1, {"path"}}},
    {"PROJECT", {{"path"}, 1, {"path"}}},
    {"HELPER", {{}, 0, {}}}, {"INTERFACE", {{}, 0, {}}},
    {"TICK_IMPL", {{}, 0, {}}}, {"GLOBAL", {{}, 0, {}}},
    {"SIMULATION", {{}, 0, {}}},
};

const std::map<string, string> removed = {
    {"ALIAS_ARRAY1", "ALIAS(..., dims=[N])"},
    {"ALIAS_ARRAY2", "ALIAS(..., dims=[N1,N2])"},
    {"REGISTER_MUL", "REGISTER(..., ports=N)"},
    {"REGISTER_ARRAY1", "REGISTER(..., ports=P, dims=[N])"},
    {"REQUEST_READY", "REQUEST(..., handshake=true)"},
    {"SERVICE_READY", "SERVICE(..., ready=condition)"},
    {"SERVICE_PRIO", "SERVICE(..., priority=P)"},
    {"SERVICE_PRIO_READY", "SERVICE(..., priority=P, ready=condition)"},
    {"CHILD_INSTANCE_ARRAY1", "CHILD_INSTANCE(..., dims=[N])"},
    {"CHILD_INSTANCE_ARRAY2", "CHILD_INSTANCE(..., dims=[N1,N2])"},
    {"USE_CHILD_SERVICE_PORT", "USE_CHILD_SERVICE(...)"},
    {"BRAM_1RW", "BRAM(..., mode=1rw)"},
    {"QUEUE_MP", "QUEUE(..., enq_width=E, deq_width=D)"},
    {"REQUEST_PORT", "REQUEST(...)"}, {"SERVICE_PORT", "SERVICE(...)"},
    {"SERVICE_COND_IMPL", "SERVICE(..., ready=condition) { ... }"},
    {"SERVICE_LOGIC_IMPL", "SERVICE(...) { ... }"},
};
}

MacroEntry normalizeDeclaration(const MacroEntry &entry, const string &location) {
    if (entry.name == "PIPELINE") {
        MacroEntry attributes = entry;

        size_t count = 0;
        while (count < entry.args.size()) {
            auto a = trim(entry.args[count]);
            const auto open = a.find('(');
            const string kind = trim(a.substr(0, open));
            if (kind == "IREG" || kind == "OREG" || kind == "IVALID" || kind == "OVALID") break;
            ++count;
        }
        // Normalize fixed attributes independently of the nested declaration tail.
        attributes.args.resize(count);
        attributes.name = "PIPELINE_ATTR";
        auto normalized = normalizeDeclaration(attributes, location);
        normalized.name = "PIPELINE";
        normalized.arg_pos.assign(normalized.args.size(), entry.pos);
        for (size_t i = count; i < entry.args.size(); ++i) {
            normalized.args.push_back(entry.args[i]);
            normalized.arg_pos.push_back(i < entry.arg_pos.size() ? entry.arg_pos[i] : entry.pos);
        }
        return normalized;
    }
    auto fail = [&](const string &message) -> void {
        const string position = location.empty()
            ? "line " + std::to_string(entry.pos.line + 1) + ", column " + std::to_string(entry.pos.column + 1)
            : location;
        throw VulException((entry.name == "PIPELINE_ATTR" ? string("PIPELINE") : entry.name) + " " + message + " at " + position);
    };
    if (auto old = removed.find(entry.name); old != removed.end())
        fail("has been removed; use " + old->second);
    auto found = specs.find(entry.name);
    if (found == specs.end()) return entry; // Scope registries diagnose unknown macros.
    const auto &spec = found->second;
    std::map<string, string> values;
    vector<string> tail;
    std::set<string> instance_keys;
    bool named = false, business = false;
    size_t positional = 0;
    for (const auto &raw : entry.args) {
        const string arg = trim(raw);
        if (arg.empty()) fail("has an empty argument");
        if (arg.starts_with("ARG(") || arg.starts_with("RESP(")) {
            if (!spec.business) fail("does not accept ARG/RESP parameters");
            business = true;
            tail.push_back(arg);
            continue;
        }
        if (business) fail("attributes must precede ARG/RESP parameters");
        auto attr = parseKeyValueArg(arg);
        string key, value;
        if (attr) {
            named = true;
            key = attr->first;
            value = attr->second;
            if (spec.instance && key.starts_with("COORD("))
                fail("COORD(dim)=name has been removed; use PARAM(name)=COORD(dim)");
            if (spec.instance && key.starts_with("PARAM(") && key.back() == ')') {
                pair<string, string> parameter;
                try {
                    parameter = parseParameterOverrideArg(arg, entry.name);
                    parseCoordinateBindingValue(parameter.second);
                } catch (const VulException &e) { fail(e.getError().msg); }
                key = "PARAM(" + parameter.first + ")";
                // A parameter has one slot whether its value is a constant or
                // an instance coordinate. Whitespace cannot bypass uniqueness.
                if (!instance_keys.insert(parameter.first).second)
                    fail("attribute '" + key + "' is specified more than once");
                tail.push_back(key + "=" + parameter.second);
                continue;
            }
            if (std::find(spec.keys.begin(), spec.keys.end(), key) == spec.keys.end()) {
                string hint;
                if (key == "array" || key == "dim") hint = "; use dims=[...]";
                if (key == "portnum") hint = "; use ports=...";
                if (spec.instance) hint = "; parameter overrides require PARAM(name)=value";
                fail("has unknown attribute '" + key + "'" + hint);
            }
        } else {
            if (arg.starts_with("ARRAY(")) fail("ARRAY(...) has been removed; use dims=[...]");
            if (named) fail("positional attributes cannot follow named attributes");
            if (positional >= spec.keys.size()) fail("has too many positional arguments");
            key = spec.keys[positional++];
            value = arg;
        }
        if (value.empty()) fail("attribute '" + key + "' cannot be empty");
        if (!values.emplace(key, value).second)
            fail("attribute '" + key + "' is specified more than once");
        if (key == "dims") {
            try {
                vector<string> dims;
                appendDimList(dims, value);
            } catch (const VulException &e) { fail(e.getError().msg); }
        }
    }
    for (const auto &key : spec.required)
        if (!values.count(key)) fail("requires parameter '" + key + "'");
    MacroEntry out = entry;
    out.args.clear();
    for (size_t i = 0; i < spec.keys.size(); ++i) {
        const auto &key = spec.keys[i];
        if (auto value = values.find(key); value != values.end())
            out.args.push_back(i < spec.prefix ? value->second : key + "=" + value->second);
    }
    out.args.insert(out.args.end(), tail.begin(), tail.end());
    return out;
}
