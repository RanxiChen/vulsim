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
#include "common.hpp"
#include "simgen.h"
#include "debugmap.hpp"

namespace rtlgen {
namespace detail {
void _procConstAndBundle(RTLGenContext &ctx){
    VulErrorContextGuard guard("processing local configlib and bundlelib");

    for (const auto &cfg_entry : ctx.local_configlib) {
        const string &cfg_name = cfg_entry.first;
        ConfigRealValue real_value = cfg_entry.second;
        const string definition =
            "#define " + cfg_name + " (" + std::to_string(real_value) + ")\n";
        vulDebugAppendLine(
            ctx.hls_header,
            ctx.hls_header_debug,
            definition
        );
    }
    if (!ctx.local_configlib.empty()) {
        vulDebugAppendLine(ctx.hls_header, ctx.hls_header_debug, "\n");
    }

    for (const auto &bundle : ctx.local_bundlelib) {
        auto bundle_lines = simgen::genStaticBundle(bundle);
        ctx.hls_header.insert(ctx.hls_header.end(), bundle_lines.begin(), bundle_lines.end());
        ctx.hls_header.push_back("\n");
    }

    for (const auto &line : ctx.global_helper_codes) {
        if (line.ends_with("\n")) {
            ctx.hls_header.push_back(line);
        } else {
            ctx.hls_header.push_back(line + "\n");
        }
    }
    if (!ctx.global_helper_codes.empty()) {
        ctx.hls_header.push_back("\n");
    }

    vulDebugAppendLines(ctx.hls_header, ctx.hls_header_debug, ctx.module.helper_codes, ctx.module.helper_codes_debug);
    if (!ctx.module.helper_codes.empty()) {
        ctx.hls_header.push_back("\n");
        ctx.hls_header_debug.push_back({});
    }
};

void _procWires(RTLGenContext &ctx) {
    VulErrorContextGuard guard("processing wires");

    for (const auto &wire : ctx.module.wires) {
        string def = wire.signature.toString() + " " + wire.name + ";\n";
        ctx.hls_init.push_back(def);
        ctx.hls_init_debug.push_back({});
        ctx.hls_init.push_back("{\n");
        ctx.hls_init_debug.push_back({});
        vulDebugAppendLines(ctx.hls_init, ctx.hls_init_debug, wire.reset_codelines, wire.reset_codelines_debug);
        ctx.hls_init.push_back("}\n");
        ctx.hls_init_debug.push_back({});
    }
};


} // namespace detail

} // namespace rtlgen
