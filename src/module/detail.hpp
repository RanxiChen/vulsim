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

#include "module.h"

namespace vulmodule::detail {

struct InstantiationContext {
    VulStaticModuleInstance &instance;
    const VulTempModule &temp;
    VulStaticConfigLib local_config_lib;
    VulStaticBundleLib local_config_library;
    VulLogicBlockID next_logic_block_id = 1;

    InstantiationContext(VulStaticModuleInstance &instance, const VulTempModule &temp,
                         const VulStaticConfigLib &global_config)
        : instance(instance), temp(temp), local_config_lib(global_config) {}
};

void staticalizeTransactions(InstantiationContext &ctx);
void staticalizeResources(InstantiationContext &ctx);
void staticalizeChildren(InstantiationContext &ctx);
void staticalizeTicks(InstantiationContext &ctx);
void staticalizeConnections(InstantiationContext &ctx);

string joinCode(const vector<string> &lines);
vector<std::pair<size_t, string>> transactionCallIndices(string text, const string &name);
string activeConstexprCode(string text, const VulStaticConfigLib &config);

} // namespace vulmodule::detail
