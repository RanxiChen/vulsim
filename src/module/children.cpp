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

namespace vulmodule::detail {

void staticalizeChildren(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    // instances
    for (const auto &inst : temp.instances) {
        VulErrorContextGuard _err{"Processing instance '" + inst.name + "'"};
        VulStaticInstanceDecl static_inst;
        static_inst.name = inst.name;
        static_inst.module_name = inst.module_name;
        for (const auto &dim_expr : inst.array_dims) {
            ConfigRealValue dim_value = calculateConstexprValue(dim_expr, local_config_lib);
            if (dim_value <= 0) {
                throw VulException("Child instance array dimension must be positive for '" + inst.name + "'");
            }
            static_inst.array_dims.push_back(dim_value);
        }
        std::unordered_set<string> bound_names;
        for (const auto &[dim_expr, name] : inst.coordinate_bindings) {
            const auto dim = calculateConstexprValue(dim_expr, local_config_lib);
            if (dim < 0 || dim >= static_cast<ConfigRealValue>(static_inst.array_dims.size()))
                throw VulException("COORD dimension out of range for '" + inst.name + "'");
            if (!static_inst.coordinate_bindings.emplace(static_cast<uint32_t>(dim), name).second || !bound_names.insert(name).second)
                throw VulException("Duplicate COORD binding for '" + inst.name + "'");
        }
        for (const auto &param_override : inst.parameter_overrides) {
            const string &param_name = param_override.first;
            const string &param_value_str = param_override.second;
            if (bound_names.contains(param_name))
                throw VulException("COORD-bound parameter cannot be overridden: " + param_name);
            static_inst.parameter_expressions[param_name] = param_value_str;
            ConfigRealValue param_value = calculateConstexprValue(param_value_str, local_config_lib);
            static_inst.parameter_overrides[param_name] = param_value;
        }
        instance.instances[inst.name] = static_inst;
    }

}

} // namespace vulmodule::detail
