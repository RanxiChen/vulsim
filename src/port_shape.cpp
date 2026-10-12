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

#include "port_shape.h"
#include "configexpr.hpp"
#include <regex>

// Coordinates are expressions, not a list wrapped in parentheses. Angle
// brackets here are comparison operators and must not hide following commas.
static vector<string> splitCoordinates(const string &text) {
    vector<string> result;
    if (text.find_first_not_of(" \t\r\n") == string::npos) return result;
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || (!quote && depth == 0 && text[i] == ',')) {
            auto item = text.substr(start, i - start);
            auto first = item.find_first_not_of(" \t\r\n");
            if (first != string::npos)
                result.push_back(item.substr(first, item.find_last_not_of(" \t\r\n") - first + 1));
            else throw VulException("Empty transaction coordinate expression");
            start = i + 1;
            continue;
        }
        const char c = text[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') quote = c;
        else if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
    }
    return result;
}

string portArrayType(string type, const vector<ConfigRealValue> &dims) {
    if (type == "void") return type;
    for (auto it = dims.rbegin(); it != dims.rend(); ++it)
        type = "std::array<" + type + ", " + std::to_string(*it) + ">";
    return type;
}
vector<ConfigRealValue> portCoordinates(uint32_t index, const vector<ConfigRealValue> &dims) {
    vector<ConfigRealValue> out(dims.size());
    for (size_t d = dims.size(); d-- > 0;) { out[d] = index % dims[d]; index /= dims[d]; }
    return out;
}
string portSubscript(uint32_t index, const vector<ConfigRealValue> &dims) {
    string out;
    for (auto i : portCoordinates(index, dims)) out += "[" + std::to_string(i) + "]";
    return out;
}
string portIndexExpression(const string &coordinates, const vector<ConfigRealValue> &dims) {
    auto args = splitCoordinates(coordinates);
    if (args.size() != dims.size()) throw VulException("Transaction coordinate count does not match rank");
    string out = "0";
    for (size_t d = 0; d < dims.size(); ++d)
        out = "((" + out + ") * " + std::to_string(dims[d]) + " + (" + args[d] + "))";
    return out;
}
string portFlatName(const string &name) { return "__vul_flat_" + name; }
string portBulkArguments(const VulStaticReqServ &port) {
    string out;
    for (const auto &ref : port.param_order) {
        if (!out.empty()) out += ", ";
        auto &param = ref.kind == VulReqServParamKind::Arg ? port.args.at(ref.index) : port.rets.at(ref.index);
        out += portArrayType(param.type.toString(), port.port_dims);
        out += ref.kind == VulReqServParamKind::Resp ? " & " : " ";
        out += param.name;
    }
    return out;
}
string portBulkBody(const VulStaticReqServ &port, const string &callee, bool lambda) {
    string out;
    if (port.has_handshake) out += portArrayType("bool", port.port_dims) + " __vul_accepted{};\n";
    for (uint32_t i = 0; i < port.array_size; ++i) {
        const auto sub = portSubscript(i, port.port_dims);
        if (port.has_handshake) out += "__vul_accepted" + sub + " = ";
        out += callee + (lambda ? ".template operator()" : "") + "<" + std::to_string(i) + ">(";
        string args;
        for (auto ref : port.param_order) {
            if (!args.empty()) args += ", ";
            args += (ref.kind == VulReqServParamKind::Arg ? port.args.at(ref.index).name : port.rets.at(ref.index).name) + sub;
        }
        out += args + ");\n";
    }
    if (port.has_handshake) out += "return __vul_accepted;\n";
    return out;
}
string portRequestFacade(const VulStaticReqServ &port) {
    if (!port.is_arrayed) return "";
    string params, coords, checks;
    for (size_t d = 0; d < port.port_dims.size(); ++d) {
        const string coord = "__vul_coord_" + std::to_string(d);
        if (d) { params += ", "; coords += ", "; }
        params += "uint32_t " + coord; coords += coord;
        checks += "static_assert(" + coord + " < " + std::to_string(port.port_dims[d]) + ", \"Transaction coordinate out of range\");\n";
    }
    return "template<" + params + ">\n" + port.returnType() + " " + port.name + "(" + port.signatureArgOnly() + ") {\n" + checks
        + (port.has_handshake ? "return " : "") + portFlatName(port.name) + "<" + portIndexExpression(coords, port.port_dims) + ">(" + port.signatureArgNameList() + ");\n}\n"
        + portArrayType(port.returnType(), port.port_dims) + " " + port.name + "(" + portBulkArguments(port) + ") {\n"
        + portBulkBody(port, portFlatName(port.name)) + "}\n";
}
vector<uint32_t> portCallSlots(const vector<ConfigRealValue> &dims, const string &coordinates, const VulStaticConfigLib &config) {
    if (coordinates.empty()) {
        uint32_t count = 1; for (auto dim : dims) count *= dim;
        vector<uint32_t> out; for (uint32_t i = 0; i < count; ++i) out.push_back(i); return out;
    }
    const auto args = splitCoordinates(coordinates);
    if (args.size() != dims.size()) throw VulException("Transaction coordinate count does not match rank");
    uint32_t index = 0;
    for (size_t d = 0; d < dims.size(); ++d) {
        const auto value = calculateConstexprValue(args[d], config);
        if (value < 0 || value >= dims[d]) throw VulException("Transaction coordinate out of range");
        index = index * dims[d] + value;
    }
    return {index};
}
string lowerPortIndexDims(string code, const vector<ConfigRealValue> &dims, const VulStaticConfigLib &config) {
    // Mask literals and comments while retaining source offsets.
    string mask = code;
    for (size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] == '"' || mask[i] == '\'') {
            char quote = mask[i]; mask[i++] = ' ';
            for (; i < mask.size(); ++i) { char c = mask[i]; mask[i] = ' '; if (c == '\\') { if (++i < mask.size()) mask[i] = ' '; } else if (c == quote) break; }
        } else if (i + 1 < mask.size() && mask[i] == '/' && (mask[i+1] == '/' || mask[i+1] == '*')) {
            bool block = mask[i+1] == '*'; mask[i++] = ' '; mask[i] = ' ';
            for (++i; i < mask.size(); ++i) {
                char c = mask[i]; mask[i] = c == '\n' ? '\n' : ' ';
                if (!block && c == '\n') break;
                if (block && c == '*' && i + 1 < mask.size() && mask[i+1] == '/') { mask[++i] = ' '; break; }
            }
        }
    }
    const std::regex name(R"(\bIDX_DIM\s*\()");
    vector<std::tuple<size_t,size_t,string>> replacements;
    for (std::sregex_iterator it(mask.begin(), mask.end(), name), end; it != end; ++it) {
        size_t begin = it->position(), open = begin + it->length() - 1, close = open + 1;
        int depth = 1;
        for (; close < mask.size(); ++close) { if (mask[close] == '(') ++depth; if (mask[close] == ')' && --depth == 0) break; }
        if (close == mask.size()) throw VulException("Unbalanced IDX_DIM expression");
        const auto expression = code.substr(open + 1, close - open - 1);
        auto coordinate = [&](ConfigRealValue d) {
            if (d < 0 || uint64_t(d) >= dims.size()) throw VulException("IDX_DIM dimension out of range");
            uint64_t stride = 1; for (size_t i = d + 1; i < dims.size(); ++i) stride *= dims[i];
            return "((IDX / " + std::to_string(stride) + ") % " + std::to_string(dims[d]) + ")";
        };
        string lowered;
        if (!config.contains("IDX") && std::regex_search(expression, std::regex(R"(\bIDX\b)"))) {
            // IDX is a template constant in generated Service implementations.
            // Validate every specialization, then retain its constant selector.
            uint32_t count = 1; for (auto dim : dims) count *= dim;
            auto specialization = config;
            for (uint32_t slot = 0; slot < count; ++slot) {
                specialization["IDX"] = slot;
                coordinate(calculateConstexprValue(expression, specialization));
            }
            if (dims.empty()) throw VulException("IDX_DIM dimension out of range");
            lowered = coordinate(dims.size() - 1);
            for (size_t d = dims.size() - 1; d-- > 0;)
                lowered = "((" + expression + ") == " + std::to_string(d) + " ? " + coordinate(d) + " : " + lowered + ")";
        } else lowered = coordinate(calculateConstexprValue(expression, config));
        replacements.push_back({begin, close + 1 - begin, lowered});
    }
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) code.replace(std::get<0>(*it), std::get<1>(*it), std::get<2>(*it));
    return code;
}
vector<string> lowerPortIndexDims(const vector<string> &code, const vector<ConfigRealValue> &dims, const VulStaticConfigLib &config) {
    string joined; for (auto &line : code) joined += line + "\n";
    joined = lowerPortIndexDims(joined, dims, config);
    vector<string> out; size_t start = 0;
    for (size_t i = 0; i < joined.size(); ++i) if (joined[i] == '\n') { out.push_back(joined.substr(start, i-start)); start = i+1; }
    return out;
}
