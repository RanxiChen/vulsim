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

void staticalizeResources(InstantiationContext &ctx) {
    auto &instance = ctx.instance;
    const auto &temp = ctx.temp;
    const auto &local_config_lib = ctx.local_config_lib;
    // registers
    for (const auto &reg : temp.registers) {
        VulErrorContextGuard _err{"Processing register '" + reg.name + "'"};
        VulStaticRegister static_reg;
        static_reg.name = reg.name;
        static_reg.signature = parseTypeSignature(reg.type, local_config_lib);
        static_reg.ports = 1;
        if (!reg.portnum.empty()) {
            ConfigRealValue portnum_value = calculateConstexprValue(reg.portnum, local_config_lib);
            static_reg.ports = (portnum_value > 1) ? portnum_value : 1;
        }
        static_reg.reset_codelines = reg.reset_codelines;
        static_reg.reset_codelines_debug = reg.reset_codelines_debug;
        for (const auto &dim : reg.dims) {
            ConfigRealValue dim_value = calculateConstexprValue(dim, local_config_lib);
            static_reg.dims.push_back(dim_value);
        }
        instance.registers.push_back(std::move(static_reg));
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
        if (bram.read_ports.empty() || bram.write_ports.empty()) {
            static_bram.read_ports = 0;
            static_bram.write_ports = 0;
        } else {
            static_bram.read_ports = calculateConstexprValue(bram.read_ports, local_config_lib);
            static_bram.write_ports = calculateConstexprValue(bram.write_ports, local_config_lib);
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
