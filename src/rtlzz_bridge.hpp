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

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <cstdint>

struct RTLzzLogicRTLResult {
    bool ok = false;
    std::string rtl_text;
    std::vector<std::string> debug_codelines;
    std::string error;
    std::vector<std::string> error_debug_codelines;
    std::string error_signal_debug_text;
    std::vector<std::string> error_signal_names;
    std::map<std::string, std::vector<std::uint64_t>> output_reset_values;
};

RTLzzLogicRTLResult generateLogicRTLWithRTLzz(
    const std::string &source_file,
    const std::string &top_function,
    const std::string &lib_include_dir,
    const std::vector<std::pair<std::string, std::string>> &port_bindings,
    int unroll_limit = 1024,
    bool release = false,
    bool use_circt = false,
    std::function<void(const std::string &)> progress_callback = {},
    int register_stages = 0,
    bool reset_enabled = false,
    const std::map<std::string, std::vector<std::uint64_t>> &input_reset_values = {},
    bool reset_probe = false,
    const std::string &input_valid_name = {},
    const std::string &output_valid_name = {}
);
