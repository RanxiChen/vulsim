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
#include <cassert>

namespace cppparse {

using stringop::trim;
using stringop::skip_spaces;
using stringop::split;


LinePosition findNext(
    const std::vector<std::string>& code,
    LinePosition start,
    const std::string& target
) {
    if (target.empty()) {
        return {-1, -1};
    }

    int32_t n = static_cast<int32_t>(code.size());
    if (start.line < 0 || start.line >= n) {
        return {-1, -1};
    }

    for (int32_t i = start.line; i < n; ++i) {
        const std::string& line = code[i];

        // 起始列：第一行用 start.column，其余从 0 开始
        size_t begin = (i == start.line) ? static_cast<size_t>(start.column) : 0;

        if (begin > line.size()) continue;

        size_t pos = line.find(target, begin);
        if (pos != std::string::npos) {
            return {i, static_cast<int32_t>(pos)};
        }
    }

    return {-1, -1};
}

BlockResult findNextBraceBlock(const std::vector<std::string>& code, LinePosition start, const char begin, const char end, bool keep_nextline) {
    // 1️⃣ 找到下一个 '{'
    LinePosition pos = findNext(code, start, std::string(1, begin));
    if (pos.line == -1) {
        return {{-1, -1}, ""};
    }

    // 如果先出现了一个;，代表没有合法的大括号匹配
    LinePosition semicolon_pos = findNext(code, start, ";");
    if (semicolon_pos.line != -1) {
        if (semicolon_pos.line < pos.line || (semicolon_pos.line == pos.line && semicolon_pos.column < pos.column)) {
            return {{-1, -1}, ""};
        }
    }

    int depth = 0;
    bool in_string = false;
    bool in_char = false;
    bool escape = false;

    std::string content;

    for (int32_t i = pos.line; i < (int32_t)code.size(); ++i) {
        const std::string& line = code[i];

        size_t j = (i == pos.line) ? pos.column : 0;

        for (; j < line.size(); ++j) {
            char c = line[j];

            // ===== 字符串/字符处理 =====
            if (in_string || in_char) {
                if (depth >= 1) content.push_back(c);

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
                if (depth >= 1) content.push_back(c);
                continue;
            }

            if (c == '\'') {
                in_char = true;
                if (depth >= 1) content.push_back(c);
                continue;
            }

            // ===== 大括号处理 =====
            if (c == begin) {
                if (depth >= 1) content.push_back(c);
                ++depth;
                continue;
            }

            if (c == end) {
                --depth;

                if (depth == 0) {
                    // 找到匹配的结束
                    return {{i, (int32_t)j}, content};
                }

                if (depth >= 1) content.push_back(c);
                continue;
            }

            // ===== 普通字符 =====
            if (depth >= 1) {
                content.push_back(c);
            }
        }
        if (depth >= 1 && keep_nextline) {
            content.push_back('\n');
        }
    }

    // 没有匹配到
    return {{-1, -1}, ""};
}

// 解析描述串：NAME($,$,...) -> (name, argc)
namespace {
bool parse_pattern(const std::string& pattern, std::string& name, int& argc) {
    auto l = pattern.find('(');
    auto r = pattern.find(')');
    if (l == std::string::npos || r == std::string::npos || r < l) return false;

    name = pattern.substr(0, l);

    argc = 0;
    bool has_dollar = false;
    for (size_t i = l + 1; i < r; ++i) {
        if (pattern[i] == '$') {
            ++argc;
            has_dollar = true;
        }
    }

    // 允许空参数宏：NAME()
    if (!has_dollar) argc = 0;

    return true;
}

} // namespace

std::vector<MatchMacroResult> matchMacros(const std::vector<std::string>& code, const std::string& pattern) {
    LinePosition pos{0, 0};
    std::vector<MatchMacroResult> results;
    std::string macro_name;
    int macro_argc;
    if (!parse_pattern(pattern, macro_name, macro_argc)) {
        throw VulException("Invalid macro pattern: " + pattern);
    }
    while (true) {
        pos = findNext(code, pos, macro_name);
        if (pos.line == -1) break;

        // 检查后面是否跟着 '('
        const std::string& line = code[pos.line];
        size_t after_name_pos = pos.column + macro_name.size();
        if (after_name_pos >= line.size() || line[after_name_pos] != '(') {
            pos.column = after_name_pos;
            continue; // 不是宏调用，继续搜索
        }

        // 解析括号内的内容
        auto block = findNextBraceBlock(code, pos, '(', ')', false);
        if (block.end_pos.line == -1) {
            pos.column = after_name_pos;
            continue; // 没有找到匹配的括号，继续搜索
        }

        // 拆分参数
        std::vector<std::string> args = split(block.content, ',');
        if (macro_argc != 0 && (args.size() != macro_argc)) {
            pos.column = block.end_pos.column + 1;
            continue; // 参数数量不匹配，继续搜索
        }

        // 去掉参数两端空白
        for (auto& arg : args) {
            arg = trim(arg);
        }

        results.push_back({pos, args});

        pos = {block.end_pos.line, block.end_pos.column + 1};
    }

    return results;
}

bool codeblockContainsFunctionCall(const std::vector<std::string>& code, const std::string& func_name) {
    if (func_name.empty()) return false;

    auto is_ident_char = [](char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_';
    };

    auto skip_spaces_cross_line = [&](size_t &li, size_t &ci) {
        while (li < code.size()) {
            const std::string &cur = code[li];
            while (ci < cur.size()) {
                const char c = cur[ci];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    ++ci;
                } else {
                    return;
                }
            }
            ++li;
            ci = 0;
        }
    };

    auto skip_template_arglist = [&](size_t &li, size_t &ci) -> bool {
        int depth = 0;
        bool in_string = false;
        bool in_char = false;
        bool escape = false;
        while (li < code.size()) {
            const std::string &cur = code[li];
            while (ci < cur.size()) {
                const char c = cur[ci];
                if (in_string || in_char) {
                    ++ci;
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
                    ++ci;
                    continue;
                }
                if (c == '\'') {
                    in_char = true;
                    ++ci;
                    continue;
                }
                if (c == '<') {
                    ++depth;
                    ++ci;
                    continue;
                }
                if (c == '>') {
                    --depth;
                    ++ci;
                    if (depth == 0) {
                        return true;
                    }
                    continue;
                }
                ++ci;
            }
            ++li;
            ci = 0;
        }
        return false;
    };

    for (size_t i = 0; i < code.size(); ++i) {
        const std::string& line = code[i];
        size_t pos = 0;

        while (true) {
            pos = line.find(func_name, pos);
            if (pos == std::string::npos) break;

            // 仅匹配完整标识符，避免 foo 匹配到 myfoo
            if (pos > 0 && is_ident_char(line[pos - 1])) {
                pos += func_name.size();
                continue;
            }
            size_t end = pos + func_name.size();
            if (end < line.size() && is_ident_char(line[end])) {
                pos += func_name.size();
                continue;
            }

            // 找 name 后第一个非空白字符（可跨行）
            size_t li = i;
            size_t ci = end;
            skip_spaces_cross_line(li, ci);
            if (li < code.size()) {
                if (code[li][ci] == '(') {
                    return true;
                }
                if (code[li][ci] == '<') {
                    if (skip_template_arglist(li, ci)) {
                        skip_spaces_cross_line(li, ci);
                        if (li < code.size() && ci < code[li].size() && code[li][ci] == '(') {
                            return true;
                        }
                    }
                }
            }

            pos += func_name.size();
        }
    }
    return false;
}


} // namespace cppparse
