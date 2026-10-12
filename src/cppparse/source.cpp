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
#include <fstream>

namespace cppparse {

using stringop::trim;
using stringop::skip_spaces;
using stringop::split;


std::vector<std::string> readFileLines(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);

    if (!file.is_open()) {
        throw VulException("Failed to open file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    return lines;
}

TrimResult stripComments(const std::vector<std::string>& input) {
    std::vector<std::string> out;
    std::vector<uint32_t> map;

    bool in_block_comment = false;

    std::string current_line;
    uint32_t current_origin = 0;
    bool continuing = false; // 是否处于 \ 拼接中

    for (uint32_t i = 0; i < input.size(); ++i) {
        const std::string& line = input[i];

        std::string processed;

        bool in_string = false;
        bool in_char = false;
        bool escape = false;

        for (size_t j = 0; j < line.size(); ++j) {
            char c = line[j];

            // ===== block comment =====
            if (in_block_comment) {
                if (c == '*' && j + 1 < line.size() && line[j + 1] == '/') {
                    in_block_comment = false;
                    ++j;
                }
                continue;
            }

            // ===== string / char =====
            if (in_string || in_char) {
                processed.push_back(c);

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

            // ===== start string =====
            if (c == '"') {
                in_string = true;
                processed.push_back(c);
                continue;
            }
            if (c == '\'') {
                in_char = true;
                processed.push_back(c);
                continue;
            }

            // ===== comments =====
            if (c == '/' && j + 1 < line.size()) {
                if (line[j + 1] == '/') {
                    break; // 单行注释
                }
                if (line[j + 1] == '*') {
                    in_block_comment = true;
                    ++j;
                    continue;
                }
            }

            processed.push_back(c);
        }

        // 去掉行尾空白（方便判断 \）
        stringop::rtrim_inplace(processed);

        // ===== 检查是否以 \ 结尾 =====
        bool has_backslash = false;
        if (!processed.empty() && processed.back() == '\\') {
            has_backslash = true;
            processed.pop_back(); // 去掉反斜线
            stringop::rtrim_inplace(processed);
        }

        // ===== 拼接逻辑 =====
        if (!continuing) {
            current_line = processed;
            current_origin = i;
        } else {
            current_line += processed;
        }

        continuing = has_backslash;

        // ===== 如果不再继续，输出一行 =====
        if (!continuing) {
            if (!current_line.empty()) {
                out.push_back(current_line);
                map.push_back(current_origin);
            }
            current_line.clear();
        }
    }

    // 处理文件结尾仍在拼接的情况
    if (continuing && !current_line.empty()) {
        out.push_back(current_line);
        map.push_back(current_origin);
    }

    return {out, map};
}



} // namespace cppparse
