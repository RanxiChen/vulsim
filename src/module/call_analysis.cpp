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
#include "cppparse.hpp"
#include <cctype>
#include <regex>

void detectRequestCallInLogicBlocks(VulStaticModuleInstance &module_instance) {
    vector<std::pair<string, LogicBlockCall>> valid_function_names;
    for (const auto &req_entry : module_instance.requests) {
        LogicBlockCall call;
        call.instance = "";
        call.port = req_entry.first;
        valid_function_names.push_back({req_entry.first, call});
    }
    for (const auto &use : module_instance.child_service_uses) {
        LogicBlockCall call;
        call.instance = use.instance_name;
        call.port = use.service_name;
        valid_function_names.push_back({use.alias_name, call});
    }
    for (auto &serv_entry: module_instance.serv_logic_blocks) {
        auto &serv_lb = serv_entry.second;
        for (auto &func_entry : valid_function_names) {
            if (cppparse::codeblockContainsFunctionCall(serv_lb.codelines, func_entry.first)) {
                serv_lb.call_requests.push_back(func_entry.second);
            }
        }
    }
    for (auto &tb: module_instance.tick_blocks) {
        for (auto &func_entry : valid_function_names) {
            if (cppparse::codeblockContainsFunctionCall(tb.codelines, func_entry.first)) {
                tb.call_requests.push_back(func_entry.second);
            }
        }
    }
}

namespace vulmodule::detail {

namespace {
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

string joinCode(const vector<string> &lines) {
    string out;
    const auto stripped = cppparse::stripComments(lines);
    uint32_t line_number = 1;
    for (size_t i = 0; i < stripped.lines.size(); ++i) {
        while (line_number < stripped.mapping[i]) { out += "\n"; ++line_number; }
        out += stripped.lines[i] + "\n"; ++line_number;
    }
    return out;
}
namespace {
size_t balancedEnd(const string &text, size_t start, char open, char close) {
    int depth = 0;
    bool quoted = false; char quote = 0;
    for (size_t i = start; i < text.size(); ++i) {
        if (quoted) { if (text[i] == '\\') ++i; else if (text[i] == quote) quoted = false; continue; }
        if (text[i] == '"' || text[i] == '\'') { quoted = true; quote = text[i]; continue; }
        if (text[i] == open) ++depth;
        if (text[i] == close && --depth == 0) return i;
    }
    throw VulException("Unbalanced constexpr conditional in transaction analysis");
}
string maskQuotedText(string text) {
    // Ignore names in literal text; retain offsets for source diagnostics.
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"' && text[i] != '\'') continue;
        const char quote = text[i]; text[i++] = ' ';
        for (; i < text.size(); ++i) {
            const char c = text[i]; text[i] = c == '\n' ? '\n' : ' ';
            if (c == '\\') { if (++i < text.size()) text[i] = ' '; }
            else if (c == quote) break;
        }
    }
    return text;
}
} // namespace

vector<std::pair<size_t, string>> transactionCallIndices(string text, const string &name) {
    text = maskQuotedText(std::move(text));
    vector<std::pair<size_t, string>> out;
    const std::regex identifier("\\b" + name + "\\b");
    for (std::sregex_iterator it(text.begin(), text.end(), identifier), end; it != end; ++it) {
        size_t position = it->position() + it->length();
        position = text.find_first_not_of(" \t\r\n", position);
        if (position == string::npos) continue;
        string expression;
        if (text[position] == '<') {
            const size_t begin = ++position;
            int parens = 0;
            for (; position < text.size(); ++position) {
                if (text[position] == '(') ++parens;
                else if (text[position] == ')') --parens;
                else if (text[position] == '>' && parens == 0) break;
            }
            if (position == text.size()) continue;
            expression = text.substr(begin, position - begin);
            if (expression.empty()) expression = " ";
            position = text.find_first_not_of(" \t\r\n", position + 1);
        }
        if (position < text.size() && text[position] == '(') out.push_back({it->position(), expression});
    }
    return out;
}

namespace {
bool keywordAt(const string &text, size_t position, const string &keyword) {
    return text.compare(position, keyword.size(), keyword) == 0 &&
        (position + keyword.size() == text.size() || !isIdentChar(text[position + keyword.size()]));
}
size_t statementEnd(const string &text, size_t position) {
    position = text.find_first_not_of(" \t\r\n", position);
    if (position == string::npos) throw VulException("Missing constexpr statement body");
    if (text[position] == '{') return balancedEnd(text, position, '{', '}') + 1;
    if (keywordAt(text, position, "if")) {
        const size_t open = text.find('(', position + 2);
        if (open == string::npos) throw VulException("Invalid conditional statement");
        size_t end = statementEnd(text, balancedEnd(text, open, '(', ')') + 1);
        size_t next = text.find_first_not_of(" \t\r\n", end);
        if (next != string::npos && keywordAt(text, next, "else")) end = statementEnd(text, next + 4);
        return end;
    }
    if (keywordAt(text, position, "for") || keywordAt(text, position, "while")) {
        const size_t open = text.find('(', position);
        return statementEnd(text, balancedEnd(text, open, '(', ')') + 1);
    }
    for (size_t i = position; i < text.size(); ++i) {
        if (text[i] == '(') i = balancedEnd(text, i, '(', ')');
        else if (text[i] == '{') i = balancedEnd(text, i, '{', '}');
        else if (text[i] == '[') i = balancedEnd(text, i, '[', ']');
        else if (text[i] == ';') return i + 1;
    }
    throw VulException("Unterminated constexpr statement");
}
} // namespace

string activeConstexprCode(string text, const VulStaticConfigLib &config) {
    const std::regex pattern(R"(\bif\s+constexpr\s*\()");
    const string searchable = maskQuotedText(text);
    std::smatch match;
    size_t search = 0;
    while (search < text.size()) {
        const string tail = searchable.substr(search);
        if (!std::regex_search(tail, match, pattern)) break;
        const size_t begin = search + match.position();
        const size_t paren = begin + match.length() - 1;
        const size_t endparen = balancedEnd(text, paren, '(', ')');
        const size_t body = text.find_first_not_of(" \t\r\n", endparen + 1);
        const size_t body_end = statementEnd(text, body);
        size_t end = body_end;
        size_t other = end;
        size_t next = text.find_first_not_of(" \t\r\n", end);
        if (next != string::npos && keywordAt(text, next, "else")) {
            other = text.find_first_not_of(" \t\r\n", next + 4);
            end = statementEnd(text, other);
        }
        const bool enabled = calculateConstexprValue(text.substr(paren + 1, endparen - paren - 1), config) != 0;
        size_t selected = enabled ? body : other;
        size_t selected_end = enabled ? body_end : end;
        if (selected < selected_end && text[selected] == '{') { ++selected; --selected_end; }
        string chosen = activeConstexprCode(text.substr(selected, selected_end - selected), config);
        string replacement = text.substr(begin, end - begin);
        for (auto &c : replacement) if (c != '\n') c = ' ';
        replacement.replace(selected - begin, chosen.size(), chosen);
        text.replace(begin, end - begin, replacement);
        search = end;
    }
    return text;
}

} // namespace vulmodule::detail
