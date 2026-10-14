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
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace vulmodule::detail {

void staticalizeResources(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    // registers
    for (const auto &reg : temp.registers) {
        VulErrorContextGuard _err{"Processing register '" + reg.name + "'"};
        VulStaticRegister static_reg;
        static_reg.name = reg.name;
        static_reg.role = reg.role;
        static_reg.declaration_loc = reg.declaration_loc;
        VulErrorContextGuard register_location{reg.declaration_loc.file + ":" + std::to_string(reg.declaration_loc.line)};
        static_reg.signature = parseTypeSignature(reg.type, local_config_lib);
        static_reg.ports = 1;
        if (!reg.portnum.empty()) {
            ConfigRealValue portnum_value = calculateConstexprValue(reg.portnum, local_config_lib);
            static_reg.ports = (portnum_value > 1) ? portnum_value : 1;
        }
        if (reg.role != VulRegisterRole::Ordinary) {
            const auto ports = reg.portnum.empty() ? 1 : calculateConstexprValue(reg.portnum, local_config_lib);
            if (ports < 1 || ports >= 64 || (pipelineOutput(reg.role) && ports != 1))
                throw VulException("Invalid pipeline register ports: " + reg.name);
            if (!reg.dims.empty()) throw VulException("Pipeline register does not accept dims: " + reg.name);
            // Follow value aliases, but do not reject arrays contained in a struct.
            auto type = static_reg.signature.type;
            std::unordered_set<string> visited;
            while (visited.insert(type).second) {
                string compact = type;
                compact.erase(std::remove_if(compact.begin(), compact.end(), [](unsigned char c) { return std::isspace(c); }), compact.end());
                if (compact.starts_with("std::array<") || compact.find('[') != string::npos)
                    throw VulException("Pipeline register requires a non-array type: " + reg.name);
                auto alias = std::find_if(ctx.local_config_library.begin(), ctx.local_config_library.end(), [&](const auto &bundle) { return bundle.name == type; });
                if (alias == ctx.local_config_library.end() || !alias->is_alias || alias->members.empty()) break;
                const auto &target = alias->members.front();
                if (!target.dims.empty()) throw VulException("Pipeline register requires a non-array type: " + reg.name);
                type = target.type.type;
            }
        }
        static_reg.reset_codelines = reg.reset_codelines;
        static_reg.reset_codelines_debug = reg.reset_codelines_debug;
        for (const auto &dim : reg.dims) {
            ConfigRealValue dim_value = calculateConstexprValue(dim, local_config_lib);
            if (reg.role != VulRegisterRole::Ordinary && dim_value <= 0) throw VulException("Pipeline array dimension must be positive");
            static_reg.dims.push_back(dim_value);
        }
        instance.registers.push_back(std::move(static_reg));
    }

    for (const auto &pipe : temp.pipelines) {
        VulStaticPipeline result;
        result.name = pipe.name;
        result.declaration_loc = pipe.declaration_loc;
        VulErrorContextGuard pipeline_location{pipe.declaration_loc.file + ":" + std::to_string(pipe.declaration_loc.line)};
        auto latency = calculateConstexprValue(pipe.latency, local_config_lib);
        if (latency <= 0 || latency > 2147483647) throw VulException("PIPELINE latency must be a positive int: " + pipe.name);
        result.latency = static_cast<int>(latency);
        result.endpoints = pipe.endpoints;
        result.codelines = pipe.codelines;
        result.codelines_debug = pipe.codelines_debug;
        instance.pipelines.push_back(std::move(result));
    }

    // wires
    for (const auto &wire : temp.wires) {
        VulErrorContextGuard _err{"Processing wire '" + wire.name + "'"};
        VulStaticWire static_wire;
        static_wire.name = wire.name;
        static_wire.signature = parseTypeSignature(wire.type, local_config_lib);
        static_wire.reset_codelines = wire.reset_codelines;
        static_wire.reset_codelines_debug = wire.reset_codelines_debug;
        instance.wires.push_back(std::move(static_wire));
    }

    auto log2ceil = [](uint64_t x) -> uint64_t {
        if (x == 0) return 0;
        uint64_t power = 1;
        uint64_t exp = 0;
        while (power < x) {
            power <<= 1;
            ++exp;
        }
        return exp;
    };

    // brams
    for (const auto &bram : temp.brams) {
        VulErrorContextGuard _err{"Processing BRAM '" + bram.name + "'"};
        VulStaticBRAM static_bram;
        static_bram.name = bram.name;
        static_bram.data_type = parseTypeSignature(bram.data_type, local_config_lib);
        static_bram.addr_size = calculateConstexprValue(bram.addr_size, local_config_lib);
        if (static_bram.addr_size <= 1) {
            throw VulException("BRAM addr_size must be greater than 1");
        }
        static_bram.addr_width = static_cast<ConfigRealValue>(log2ceil(static_cast<uint64_t>(static_bram.addr_size)));
        const auto read_ports = calculateConstexprValue(bram.read_ports, local_config_lib);
        const auto write_ports = calculateConstexprValue(bram.write_ports, local_config_lib);
        if (bram.mode_1rw) {
            if (read_ports != 1 || write_ports != 1)
                throw VulException("BRAM mode=1rw requires read_ports=1 and write_ports=1");
            static_bram.read_ports = 0;
            static_bram.write_ports = 0;
        } else {
            static_bram.read_ports = read_ports;
            static_bram.write_ports = write_ports;
        }
        instance.brams.push_back(std::move(static_bram));
    }

    // digital ROMs
    for (const auto &rom : temp.roms) {
        VulErrorContextGuard _err{"Processing ROM '" + rom.name + "'"};
        VulStaticDigitalROM static_rom;
        static_rom.name = rom.name;
        static_rom.data_width = calculateConstexprValue(rom.data_width, local_config_lib);
        static_rom.addr_size = calculateConstexprValue(rom.addr_size, local_config_lib);
        if (static_rom.addr_size <= 1) {
            throw VulException("ROM addr_size must be greater than 1");
        }
        static_rom.addr_width = static_cast<ConfigRealValue>(log2ceil(static_cast<uint64_t>(static_rom.addr_size)));
        static_rom.read_ports = calculateConstexprValue(rom.read_ports, local_config_lib);
        static_rom.init_path = rom.init_path;
        instance.roms.push_back(std::move(static_rom));
    }

    // queues
    for (const auto &queue : temp.queues) {
        VulErrorContextGuard _err{"Processing queue '" + queue.name + "'"};
        VulStaticQueue static_queue;
        static_queue.name = queue.name;
        static_queue.type = parseTypeSignature(queue.type, local_config_lib);
        static_queue.depth = calculateConstexprValue(queue.depth, local_config_lib);
        static_queue.enq_width = calculateConstexprValue(queue.enq_width, local_config_lib);
        static_queue.deq_width = calculateConstexprValue(queue.deq_width, local_config_lib);
        instance.queues.push_back(std::move(static_queue));
    }

}

} // namespace vulmodule::detail
