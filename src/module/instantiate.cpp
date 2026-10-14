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
#include <regex>

void instantiateModule(
    VulStaticModuleInstance &instance,
    const VulTempModule &temp,
    const VulStaticConfigLib &param_overrides,
    const VulStaticConfigLib &global_config,
    const VulStaticBundleLib &global_bundles
) {
    auto reject_internal = [](const vector<string> &lines) {
        const std::regex reserved(R"(\b__vul_(?:idx_|array_idx_|store_|pipeline_)\w*)");
        for (const auto &line : cppparse::stripComments(lines).lines)
            if (std::regex_search(line, reserved)) throw VulException("Internal generated names are not a user API");
    };
    reject_internal(temp.helper_codes);
    for (const auto &pipe : temp.pipelines) reject_internal(pipe.codelines);
    for (const auto &reg : temp.registers) reject_internal(reg.reset_codelines);
    for (const auto &wire : temp.wires) reject_internal(wire.reset_codelines);
    for (const auto &block : temp.tick_blocks) reject_internal(block);
    for (const auto &service : temp.services) reject_internal(service.codelines);
    for (const auto &query : temp.queries) reject_internal(query.codelines);
    instance.filepath = temp.filepath;
    instance.module_name = temp.name;

    vulmodule::detail::InstantiationContext ctx(instance, temp, global_config);
    auto &local_config_lib = ctx.local_config_lib;

    for (const auto &conf : temp.configs) {
        VulErrorContextGuard _err{"Processing config '" + conf.name + "'"};
        ConfigRealValue value = calculateConstexprValue(conf.value, local_config_lib);
        local_config_lib[conf.name] = value;
        instance.local_consts[conf.name] = value;
    }
    for (const auto &param : temp.params) {
        VulErrorContextGuard _err{"Processing parameter '" + param.name + "'"};
        ConfigRealValue value;
        auto override_iter = param_overrides.find(param.name);
        if (override_iter != param_overrides.end()) {
            value = override_iter->second;
        } else {
            value = calculateConstexprValue(param.value, local_config_lib);
        }
        local_config_lib[param.name] = value;
        instance.local_parameters[param.name] = value;
    }

    for (const auto &bundle : temp.bundles) {
        VulErrorContextGuard _err{"Processing bundle '" + bundle.name + "'"};
        VulStaticBundle sb = staticalizeBundle(bundle, local_config_lib);
        instance.local_bundles.push_back(sb);
    }
    ctx.local_config_library = mergeStaticBundleLibs(global_bundles, instance.local_bundles);

    vulmodule::detail::staticalizeTransactions(ctx);
    vulmodule::detail::staticalizeResources(ctx);
    vulmodule::detail::staticalizeChildren(ctx);
    vulmodule::detail::staticalizeTicks(ctx);
    vulmodule::detail::staticalizeConnections(ctx);
    // helper codes
    instance.helper_codes = temp.helper_codes;
    instance.helper_codes_debug = temp.helper_codes_debug;
}
