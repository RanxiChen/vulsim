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

#include "cppparse.hpp"
#include "stringop.hpp"
#include "errormsg.hpp"
#include <cctype>
#include <string_view>

namespace cppparse {

using stringop::trim;
using stringop::skip_spaces;
using stringop::split;


using namespace stringop;

namespace {

struct FlatCode {
    std::string text;
    std::vector<LinePosition> pos;
};

bool isIdentStart(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_';
}

bool isIdentChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_';
}

bool isSpace(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isspace(u);
}

std::string locStr(const std::vector<LinePosition>& pos, size_t i) {
    if (i >= pos.size()) {
        return "<eof>";
    }

    return "line " + std::to_string(pos[i].line) +
           ", column " + std::to_string(pos[i].column);
}

FlatCode flattenCodeWithoutPreprocessorLines(const std::vector<std::string>& code) {
    FlatCode out;

    for (int32_t line = 0; line < static_cast<int32_t>(code.size()); ++line) {
        const std::string& s = code[line];

        if (!ltrim(s).empty() && ltrim(s)[0] == '#') {
            continue;
        }

        for (int32_t col = 0; col < static_cast<int32_t>(s.size()); ++col) {
            out.text.push_back(s[col]);
            out.pos.push_back({line, col});
        }

        out.text.push_back('\n');
        out.pos.push_back({line, static_cast<int32_t>(s.size())});
    }

    return out;
}

FlatCode flattenCodeWithoutPreprocessorLines(
    const std::vector<std::string>& code,
    const std::vector<LinePosition>& line_positions
) {
    FlatCode out;

    for (int32_t line = 0; line < static_cast<int32_t>(code.size()); ++line) {
        const std::string& s = code[line];

        if (!ltrim(s).empty() && ltrim(s)[0] == '#') {
            continue;
        }

        LinePosition base_pos{line, 0};
        if (line < static_cast<int32_t>(line_positions.size())) {
            base_pos = line_positions[line];
        }

        for (int32_t col = 0; col < static_cast<int32_t>(s.size()); ++col) {
            out.text.push_back(s[col]);
            out.pos.push_back({base_pos.line, base_pos.column + col});
        }

        out.text.push_back('\n');
        out.pos.push_back({base_pos.line, base_pos.column + static_cast<int32_t>(s.size())});
    }

    return out;
}

size_t skipInterEntryTokens(const std::string& s, size_t p) {
    while (p < s.size()) {
        if (isSpace(s[p]) || s[p] == ';') {
            ++p;
        } else {
            break;
        }
    }

    return p;
}

size_t skipSpacesOnly(const std::string& s, size_t p) {
    while (p < s.size() && isSpace(s[p])) {
        ++p;
    }

    return p;
}

bool isDigitForBase(char c, unsigned base) {
    unsigned value;
    if (c >= '0' && c <= '9') {
        value = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        value = static_cast<unsigned>(c - 'a') + 10;
    } else if (c >= 'A' && c <= 'F') {
        value = static_cast<unsigned>(c - 'A') + 10;
    } else {
        return false;
    }
    return value < base;
}

// A single quote between digits in a pp-number is a C++ digit separator,
// not the start of a character literal. Determine this before skipping
// character literals so brace scanning remains correct for literals such as
// 100'000 and 0xA'B.
bool isDigitSeparator(const std::string& s, size_t p) {
    if (p == 0 || p + 1 >= s.size()) {
        return false;
    }

    size_t numberBegin = p;
    while (numberBegin > 0) {
        const char c = s[numberBegin - 1];
        if (isIdentChar(c) || c == '.' || c == '\'') {
            --numberBegin;
        } else {
            break;
        }
    }

    if (numberBegin == p) {
        return false;
    }

    // A preprocessing number starts with a digit, or with '.' followed by a
    // digit. This excludes character literals with encoding prefixes (u'1',
    // L'1', and so on), whose preceding token begins with an identifier.
    const bool startsWithDigit = s[numberBegin] >= '0' && s[numberBegin] <= '9';
    const bool startsWithDotDigit = s[numberBegin] == '.' &&
        numberBegin + 1 < p && s[numberBegin + 1] >= '0' && s[numberBegin + 1] <= '9';
    if (!startsWithDigit && !startsWithDotDigit) {
        return false;
    }

    unsigned base = 10;
    if (startsWithDigit && s[numberBegin] == '0' && numberBegin + 1 <= p) {
        const char prefix = s[numberBegin + 1];
        if (prefix == 'x' || prefix == 'X') {
            base = 16;
        } else if (prefix == 'b' || prefix == 'B') {
            base = 2;
        } else if ((prefix >= '0' && prefix <= '9') || prefix == '\'') {
            base = 8;
        }
    }

    return isDigitForBase(s[p - 1], base) && isDigitForBase(s[p + 1], base);
}

// 跳过普通字符串、字符字面量，以及形如 R"delim(...)delim" 的 raw string。
// 如果当前位置不是字面量起点，则返回原位置。
size_t skipLiteralIfAt(const std::string& s, size_t p) {
    if (p >= s.size()) {
        return p;
    }

    // raw string literal: R"delim(...)delim"
    // 对 u8R"...", LR"...", uR"...", UR"..." 这种情况，扫描到 R 时也能处理。
    if (s[p] == 'R' && p + 1 < s.size() && s[p + 1] == '"') {
        size_t delimBegin = p + 2;
        size_t openParen = delimBegin;

        while (openParen < s.size() && s[openParen] != '(') {
            ++openParen;
        }

        if (openParen >= s.size()) {
            return s.size();
        }

        std::string delim = s.substr(delimBegin, openParen - delimBegin);
        std::string endMark = ")" + delim + "\"";

        size_t end = s.find(endMark, openParen + 1);
        if (end == std::string::npos) {
            return s.size();
        }

        return end + endMark.size();
    }

    // normal string / char literal
    if (s[p] != '"' && s[p] != '\'') {
        return p;
    }

    if (s[p] == '\'' && isDigitSeparator(s, p)) {
        return p + 1;
    }

    char quote = s[p];
    ++p;

    while (p < s.size()) {
        if (s[p] == '\\') {
            // 跳过转义字符，例如 \"、\\、\n、'\'' 等。
            p += 2;
            continue;
        }

        if (s[p] == quote) {
            return p + 1;
        }

        ++p;
    }

    return s.size();
}

size_t findMatchingByCounter(
    const std::string& s,
    const std::vector<LinePosition>& pos,
    size_t openPos,
    char openCh,
    char closeCh
) {
    if (openPos >= s.size() || s[openPos] != openCh) {
        throw VulException("internal parser error: invalid open position");
    }

    int depth = 1;
    size_t p = openPos + 1;

    while (p < s.size()) {
        size_t afterLiteral = skipLiteralIfAt(s, p);
        if (afterLiteral != p) {
            p = afterLiteral;
            continue;
        }

        if (s[p] == openCh) {
            ++depth;
            ++p;
            continue;
        }

        if (s[p] == closeCh) {
            --depth;

            if (depth == 0) {
                return p;
            }

            ++p;
            continue;
        }

        ++p;
    }

    throw VulException(
        std::string("unmatched '") + openCh + "' at " + locStr(pos, openPos)
    );
}

std::vector<std::string> splitTopLevelArgs(std::string_view sv) {
    std::vector<std::string> result;

    size_t begin = 0;
    size_t p = 0;

    int parenDepth = 0;
    int braceDepth = 0;
    int bracketDepth = 0;
    int angleDepth = 0;

    std::string tmp(sv);
    auto isTemplateAngleOpen = [&](size_t i) -> bool {
        if (i == 0 || tmp[i] != '<' || i + 1 == tmp.size() ||
            tmp[i+1] == '<' || tmp[i+1] == '=' || parenDepth || braceDepth || bracketDepth)
            return false;
        size_t prev = i;
        while (prev && isSpace(tmp[prev-1])) --prev;
        if (!prev || (!isIdentChar(tmp[prev-1]) && tmp[prev-1] != '>')) return false;
        // Only a balanced template-id protects a top-level comma. Comparisons
        // inside grouping already have their commas protected by (), [] or {}.
        int angles = 1, parens = 0, brackets = 0, braces = 0;
        for (size_t j = i + 1; j < tmp.size(); ++j) {
            size_t after = skipLiteralIfAt(tmp, j);
            if (after != j) { j = after - 1; continue; }
            const char c = tmp[j];
            if (c == '(') ++parens;
            else if (c == ')') { if (!parens) return false; --parens; }
            else if (c == '[') ++brackets;
            else if (c == ']') { if (!brackets) return false; --brackets; }
            else if (c == '{') ++braces;
            else if (c == '}') { if (!braces) return false; --braces; }
            else if (!parens && !brackets && !braces) {
                if (c == '=' || c == '?' || c == ';') return false;
                if (c == '<') ++angles;
                if (c == '>' && --angles == 0) return true;
            }
        }
        return false;
    };

    while (p < tmp.size()) {
        size_t afterLiteral = skipLiteralIfAt(tmp, p);
        if (afterLiteral != p) {
            p = afterLiteral;
            continue;
        }

        char c = tmp[p];

        if (c == '(') {
            ++parenDepth;
        } else if (c == ')') {
            if (parenDepth > 0) --parenDepth;
        } else if (c == '{') {
            ++braceDepth;
        } else if (c == '}') {
            if (braceDepth > 0) --braceDepth;
        } else if (c == '[') {
            ++bracketDepth;
        } else if (c == ']') {
            if (bracketDepth > 0) --bracketDepth;
        } else if (isTemplateAngleOpen(p)) {
            ++angleDepth;
        } else if (c == '>' && parenDepth == 0 && braceDepth == 0 && bracketDepth == 0) {
            if (angleDepth > 0) --angleDepth;
        } else if (
            c == ',' &&
            parenDepth == 0 &&
            braceDepth == 0 &&
            bracketDepth == 0 &&
            angleDepth == 0
        ) {
            result.push_back(trim(tmp.substr(begin, p - begin)));
            begin = p + 1;
        }

        ++p;
    }

    std::string last = trim(tmp.substr(begin));

    if (!last.empty() || !result.empty()) {
        result.push_back(std::move(last));
    }

    return result;
}

std::vector<std::string> splitBodyLines(std::string_view sv) {
    std::string text(sv);

    // 常见格式：
    //
    // MACRO(...) {
    //   body
    // }
    //
    // 去掉由外层大括号引入的首尾换行，避免 body 变成 ["", "...", ""]。
    if (!text.empty() && text.front() == '\n') {
        text.erase(text.begin());
    }

    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }

    std::vector<std::string> lines;
    size_t begin = 0;

    while (begin <= text.size()) {
        size_t end = text.find('\n', begin);

        if (end == std::string::npos) {
            lines.push_back(text.substr(begin));
            break;
        }

        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }

    // 空 body 返回空 vector，而不是 {""}。
    if (lines.size() == 1 && lines[0].empty()) {
        lines.clear();
    }

    return lines;
}

struct SplitBodyLinesWithPosResult {
    std::vector<std::string> lines;
    std::vector<LinePosition> positions;
};

SplitBodyLinesWithPosResult splitBodyLinesWithPos(
    const std::string &flat_text,
    const std::vector<LinePosition> &flat_pos,
    size_t begin,
    size_t end
) {
    if (begin > end) begin = end;
    if (end > flat_text.size()) end = flat_text.size();

    if (begin < end && flat_text[begin] == '\n') {
        ++begin;
    }
    if (begin < end && flat_text[end - 1] == '\n') {
        --end;
    }

    SplitBodyLinesWithPosResult out;
    if (begin == end) {
        return out;
    }

    size_t line_begin = begin;
    while (line_begin <= end) {
        size_t line_end = flat_text.find('\n', line_begin);
        if (line_end == std::string::npos || line_end > end) {
            line_end = end;
        }
        out.lines.push_back(flat_text.substr(line_begin, line_end - line_begin));
        if (line_begin < flat_pos.size()) {
            out.positions.push_back(flat_pos[line_begin]);
        } else {
            out.positions.push_back(LinePosition{-1, -1});
        }
        if (line_end == end) {
            break;
        }
        line_begin = line_end + 1;
    }

    if (out.lines.size() == 1 && out.lines[0].empty()) {
        out.lines.clear();
        out.positions.clear();
    }
    return out;
}

std::vector<MacroEntry> findAllMacroEntriesFromFlatCode(const FlatCode &flat, bool fragment = false) {

    const std::string& s = flat.text;
    const std::vector<LinePosition>& pos = flat.pos;

    std::vector<MacroEntry> entries;

    size_t p = 0;

    while (true) {
        p = skipInterEntryTokens(s, p);

        if (p >= s.size()) {
            break;
        }

        if (!isIdentStart(s[p])) {
            throw VulException(
                "expected macro name at " + locStr(pos, p)
            );
        }

        size_t nameBegin = p;
        ++p;

        while (p < s.size() && isIdentChar(s[p])) {
            ++p;
        }

        std::string name = s.substr(nameBegin, p - nameBegin);
        LinePosition entryPos = pos[nameBegin];

        p = skipSpacesOnly(s, p);

        if (p >= s.size() || s[p] != '(') {
            throw VulException(
                "expected '(' after macro name '" + name + "' at " + locStr(pos, p)
            );
        }

        size_t argOpen = p;
        size_t argClose = findMatchingByCounter(s, pos, argOpen, '(', ')');

        std::string_view argText(
            s.data() + argOpen + 1,
            argClose - argOpen - 1
        );

        std::vector<std::string> args = splitTopLevelArgs(argText);

        p = argClose + 1;
        p = skipSpacesOnly(s, p);

        MacroEntry entry;
        entry.pos = entryPos;
        entry.name = std::move(name);
        size_t cursor = argOpen + 1;
        for (const auto &arg : args) {
            auto start = s.find(arg, cursor);
            if (start == std::string::npos || start > argClose) start = cursor;
            entry.arg_pos.push_back(pos[start]);
            cursor = start + arg.size();
        }
        entry.args = std::move(args);

        if (p < s.size() && s[p] == '{') {
            size_t bodyOpen = p;
            size_t bodyClose = findMatchingByCounter(s, pos, bodyOpen, '{', '}');

            entry.has_body = true;
            auto split_body = splitBodyLinesWithPos(s, pos, bodyOpen + 1, bodyClose);
            entry.body = std::move(split_body.lines);
            entry.body_pos = std::move(split_body.positions);
            p = bodyClose + 1;
        } else if (p < s.size() && s[p] == ';') {
            entry.has_body = false;
            entry.body.clear();
            entry.body_pos.clear();
            ++p;
        } else if (fragment && p == s.size()) {
            entry.has_body = false;
        } else {
            throw VulException(
                "expected '{' or ';' after macro entry '" +
                entry.name + "' at " + locStr(pos, p)
            );
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

} // namespace

MacroEntry parseMacroFragment(const std::string &text, LinePosition origin) {
    FlatCode flat;
    flat.text = text;
    auto position = origin;
    for (char c : text) {
        flat.pos.push_back(position);
        if (c == '\n') { ++position.line; position.column = 0; }
        else ++position.column;
    }
    auto entries = findAllMacroEntriesFromFlatCode(flat, true);
    if (entries.size() != 1) throw VulException("Expected exactly one pipeline endpoint");
    return entries.front();
}

std::vector<MacroEntry> findAllMacroEntries(const std::vector<std::string>& code) {
    return findAllMacroEntriesFromFlatCode(flattenCodeWithoutPreprocessorLines(code));
}

std::vector<MacroEntry> findAllMacroEntriesPreservingLinePositions(
    const std::vector<std::string>& code,
    const std::vector<LinePosition>& line_positions
) {
    return findAllMacroEntriesFromFlatCode(flattenCodeWithoutPreprocessorLines(code, line_positions));
}



} // namespace cppparse
