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
#include "apiinline/utils.hpp"

namespace rtlgen {
namespace detail {
void _procBRAMAndROM(RTLGenContext &ctx) {
    auto escape_verilog_string = [](const string &s) -> string {
        string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\\' || c == '"') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    };

    for (const auto &bram : ctx.module.brams) {
        VulErrorContextGuard bram_guard("processing bram " + bram.name);

        uint32_t data_width = 0;
        vector<FlatField> read_fields;
        flatten_type_signature(bram.data_type, ctx.local_bundlelib, "value", data_width, read_fields);
        if (data_width == 0) {
            throw VulException("BRAM data width must be positive");
        }

        if (bram.addr_size <= 1) {
            throw VulException("BRAM addr_size must be greater than 1");
        }
        if (bram.addr_width < 0) {
            throw VulException("BRAM addr_width must be non-negative");
        }
        const uint32_t addr_width = static_cast<uint32_t>(bram.addr_width);
        const bool is_1rw = (bram.read_ports == 0 || bram.write_ports == 0);

        const string bram_name = bram.name;
        const string data_type_str = bram.data_type.toString();
        const string data_width_str = std::to_string(data_width);
        const string data_width_m1_str = std::to_string(data_width - 1);
        const string addr_width_str = std::to_string(addr_width);
        const string addr_width_m1_str = std::to_string(addr_width - 1);
        const string addr_type_str = "Int<" + addr_width_str + ">";

        if (is_1rw) {
            vector<FlatField> write_fields;
            uint32_t write_width = 0;
            flatten_type_signature(bram.data_type, ctx.local_bundlelib, "write_data", write_width, write_fields);
            if (write_width != data_width) {
                throw VulException("BRAM data width mismatch while generating proxy");
            }

            const string proxy_class_name = "__BRAM1RWProxy__" + bram_name;
            const string port_s1_en = "bram_" + bram_name + "__s1_en";
            const string port_s1_we = "bram_" + bram_name + "__s1_we";
            const string port_s1_addr = "bram_" + bram_name + "__s1_addr";
            const string port_s1_wdata = "bram_" + bram_name + "__s1_wdata";
            const string port_s2_rdata = "bram_" + bram_name + "__s2_rdata";



            ctx.hls_arguments.push_back("const Int<" + data_width_str + "> " + port_s2_rdata);
            ctx.hls_arguments.push_back("bool &" + port_s1_en);
            ctx.hls_arguments.push_back("bool &" + port_s1_we);
            ctx.hls_arguments.push_back("Int<" + addr_width_str + "> &" + port_s1_addr);
            ctx.hls_arguments.push_back("Int<" + data_width_str + "> &" + port_s1_wdata);

            ctx.hls_init.push_back(port_s1_en + " = false;\n");
            ctx.hls_init.push_back(port_s1_we + " = false;\n");
            ctx.hls_init.push_back(port_s1_addr + " = 0;\n");
            ctx.hls_init.push_back(port_s1_wdata + " = 0;\n");


            ctx.rtl_decl.push_back("wire " + port_s1_en + ";\n");
            ctx.rtl_decl.push_back("wire " + port_s1_we + ";\n");
            ctx.rtl_decl.push_back("wire [" + addr_width_m1_str + ":0] " + port_s1_addr + ";\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_s1_wdata + ";\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_s2_rdata + ";\n");

            ctx.rtl_logicports.push_back("." + port_s1_en + "(" + port_s1_en + ")");
            ctx.rtl_logicports.push_back("." + port_s1_we + "(" + port_s1_we + ")");
            ctx.rtl_logicports.push_back("." + port_s1_addr + "(" + port_s1_addr + ")");
            ctx.rtl_logicports.push_back("." + port_s1_wdata + "(" + port_s1_wdata + ")");
            ctx.rtl_logicports.push_back("." + port_s2_rdata + "(" + port_s2_rdata + ")");

            ctx.rtl_inst.push_back("VulBRAM1RW #(\n");
            ctx.rtl_inst.push_back("  .DataWidth(" + data_width_str + "),\n");
            ctx.rtl_inst.push_back("  .AddrWidth(" + addr_width_str + ")\n");
            ctx.rtl_inst.push_back(") " + bram_name + "__inst (\n");
            ctx.rtl_inst.push_back("  .clk(clk),\n");
            ctx.rtl_inst.push_back("  .rstn(rstn),\n");
            ctx.rtl_inst.push_back("  .s1_en(" + port_s1_en + "),\n");
            ctx.rtl_inst.push_back("  .s1_we(" + port_s1_we + "),\n");
            ctx.rtl_inst.push_back("  .s1_addr(" + port_s1_addr + "),\n");
            ctx.rtl_inst.push_back("  .s1_wdata(" + port_s1_wdata + "),\n");
            ctx.rtl_inst.push_back("  .s2_rdata(" + port_s2_rdata + ")\n");
            ctx.rtl_inst.push_back(");\n");
        } else {
            if (bram.read_ports <= 0 || bram.write_ports <= 0) {
                throw VulException("BRAM read_ports and write_ports must be positive");
            }
            const uint32_t read_ports = static_cast<uint32_t>(bram.read_ports);
            const uint32_t write_ports = static_cast<uint32_t>(bram.write_ports);
            const string read_ports_str = std::to_string(read_ports);
            const string write_ports_str = std::to_string(write_ports);

            vector<FlatField> write_fields;
            uint32_t write_width = 0;
            flatten_type_signature(bram.data_type, ctx.local_bundlelib, "data", write_width, write_fields);
            if (write_width != data_width) {
                throw VulException("BRAM data width mismatch while generating proxy");
            }

            const string proxy_class_name = "__BRAMProxy__" + bram_name;
            const string port_s1_readreq = "bram_" + bram_name + "__s1_readreq";
            const string port_s1_readaddr = "bram_" + bram_name + "__s1_readaddr";
            const string port_s2_readdata = "bram_" + bram_name + "__s2_readdata";
            const string port_s1_write = "bram_" + bram_name + "__s1_write";
            const string port_s1_writeaddr = "bram_" + bram_name + "__s1_writeaddr";
            const string port_s1_writedata = "bram_" + bram_name + "__s1_writedata";

            const string readreq_type_str = "std::array<bool, " + read_ports_str + ">";
            const string readaddr_type_str = "std::array<Int<" + addr_width_str + ">, " + read_ports_str + ">";
            const string readdata_type_str = "std::array<Int<" + data_width_str + ">, " + read_ports_str + ">";
            const string write_type_str = "std::array<bool, " + write_ports_str + ">";
            const string writeaddr_type_str = "std::array<Int<" + addr_width_str + ">, " + write_ports_str + ">";
            const string writedata_type_str = "std::array<Int<" + data_width_str + ">, " + write_ports_str + ">";



            ctx.hls_arguments.push_back(readreq_type_str + " &" + port_s1_readreq);
            ctx.hls_arguments.push_back(readaddr_type_str + " &" + port_s1_readaddr);
            ctx.hls_arguments.push_back("const " + readdata_type_str + " " + port_s2_readdata);
            ctx.hls_arguments.push_back(write_type_str + " &" + port_s1_write);
            ctx.hls_arguments.push_back(writeaddr_type_str + " &" + port_s1_writeaddr);
            ctx.hls_arguments.push_back(writedata_type_str + " &" + port_s1_writedata);

            for (uint32_t i = 0; i < read_ports; ++i) {
                ctx.hls_init.push_back(port_s1_readreq + "[" + std::to_string(i) + "] = false;\n");
                ctx.hls_init.push_back(port_s1_readaddr + "[" + std::to_string(i) + "] = 0;\n");
            }
            for (uint32_t i = 0; i < write_ports; ++i) {
                ctx.hls_init.push_back(port_s1_write + "[" + std::to_string(i) + "] = false;\n");
                ctx.hls_init.push_back(port_s1_writeaddr + "[" + std::to_string(i) + "] = 0;\n");
                ctx.hls_init.push_back(port_s1_writedata + "[" + std::to_string(i) + "] = 0;\n");
            }


            ctx.rtl_decl.push_back("wire " + port_s1_readreq + "[" + read_ports_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + addr_width_m1_str + ":0] " + port_s1_readaddr + "[" + read_ports_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_s2_readdata + "[" + read_ports_str + "];\n");
            ctx.rtl_decl.push_back("wire " + port_s1_write + "[" + write_ports_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + addr_width_m1_str + ":0] " + port_s1_writeaddr + "[" + write_ports_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_s1_writedata + "[" + write_ports_str + "];\n");

            ctx.rtl_logicports.push_back("." + port_s1_readreq + "(" + port_s1_readreq + ")");
            ctx.rtl_logicports.push_back("." + port_s1_readaddr + "(" + port_s1_readaddr + ")");
            ctx.rtl_logicports.push_back("." + port_s2_readdata + "(" + port_s2_readdata + ")");
            ctx.rtl_logicports.push_back("." + port_s1_write + "(" + port_s1_write + ")");
            ctx.rtl_logicports.push_back("." + port_s1_writeaddr + "(" + port_s1_writeaddr + ")");
            ctx.rtl_logicports.push_back("." + port_s1_writedata + "(" + port_s1_writedata + ")");

            ctx.rtl_inst.push_back("VulBRAM #(\n");
            ctx.rtl_inst.push_back("  .DataWidth(" + data_width_str + "),\n");
            ctx.rtl_inst.push_back("  .AddrWidth(" + addr_width_str + "),\n");
            ctx.rtl_inst.push_back("  .ReadPorts(" + read_ports_str + "),\n");
            ctx.rtl_inst.push_back("  .WritePorts(" + write_ports_str + ")\n");
            ctx.rtl_inst.push_back(") " + bram_name + "__inst (\n");
            ctx.rtl_inst.push_back("  .clk(clk),\n");
            ctx.rtl_inst.push_back("  .rstn(rstn),\n");
            ctx.rtl_inst.push_back("  .s1_readreq(" + port_s1_readreq + "),\n");
            ctx.rtl_inst.push_back("  .s1_readaddr(" + port_s1_readaddr + "),\n");
            ctx.rtl_inst.push_back("  .s2_readdata(" + port_s2_readdata + "),\n");
            ctx.rtl_inst.push_back("  .s1_write(" + port_s1_write + "),\n");
            ctx.rtl_inst.push_back("  .s1_writeaddr(" + port_s1_writeaddr + "),\n");
            ctx.rtl_inst.push_back("  .s1_writedata(" + port_s1_writedata + ")\n");
            ctx.rtl_inst.push_back(");\n");
        }
    }

    for (const auto &rom : ctx.module.roms) {
        VulErrorContextGuard rom_guard("processing rom " + rom.name);

        if (rom.data_width <= 0) {
            throw VulException("ROM data_width must be positive");
        }
        if (rom.addr_size <= 1) {
            throw VulException("ROM addr_size must be greater than 1");
        }
        if (rom.addr_width < 0) {
            throw VulException("ROM addr_width must be non-negative");
        }
        if (rom.read_ports <= 0) {
            throw VulException("ROM read_ports must be positive");
        }

        ctx.resource_files.push_back(rom.init_path);

        const uint32_t data_width = static_cast<uint32_t>(rom.data_width);
        const uint32_t addr_width = static_cast<uint32_t>(rom.addr_width);
        const uint32_t read_ports = static_cast<uint32_t>(rom.read_ports);

        const string rom_name = rom.name;
        const string data_width_str = std::to_string(data_width);
        const string data_width_m1_str = std::to_string(data_width - 1);
        const string addr_width_str = std::to_string(addr_width);
        const string addr_width_m1_str = std::to_string(addr_width - 1);
        const string read_ports_str = std::to_string(read_ports);
        const string init_path_escaped = escape_verilog_string(rom.init_path);

        const string proxy_class_name = "__ROMProxy__" + rom_name;
        const string port_s1_readreq = "rom_" + rom_name + "__s1_readreq";
        const string port_s1_readaddr = "rom_" + rom_name + "__s1_readaddr";
        const string port_s2_readdata = "rom_" + rom_name + "__s2_readdata";

        const string data_type_str = "Int<" + data_width_str + ">";
        const string addr_type_str = "Int<" + addr_width_str + ">";
        const string readreq_type_str = "std::array<bool, " + read_ports_str + ">";
        const string readaddr_type_str = "std::array<Int<" + addr_width_str + ">, " + read_ports_str + ">";
        const string readdata_type_str = "std::array<Int<" + data_width_str + ">, " + read_ports_str + ">";



        ctx.hls_arguments.push_back(readreq_type_str + " &" + port_s1_readreq);
        ctx.hls_arguments.push_back(readaddr_type_str + " &" + port_s1_readaddr);
        ctx.hls_arguments.push_back("const " + readdata_type_str + " " + port_s2_readdata);

        for (uint32_t i = 0; i < read_ports; ++i) {
            ctx.hls_init.push_back(port_s1_readreq + "[" + std::to_string(i) + "] = false;\n");
            ctx.hls_init.push_back(port_s1_readaddr + "[" + std::to_string(i) + "] = 0;\n");
        }


        ctx.rtl_decl.push_back("wire " + port_s1_readreq + "[" + read_ports_str + "];\n");
        ctx.rtl_decl.push_back("wire [" + addr_width_m1_str + ":0] " + port_s1_readaddr + "[" + read_ports_str + "];\n");
        ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_s2_readdata + "[" + read_ports_str + "];\n");

        ctx.rtl_logicports.push_back("." + port_s1_readreq + "(" + port_s1_readreq + ")");
        ctx.rtl_logicports.push_back("." + port_s1_readaddr + "(" + port_s1_readaddr + ")");
        ctx.rtl_logicports.push_back("." + port_s2_readdata + "(" + port_s2_readdata + ")");

        ctx.rtl_inst.push_back("VulROM #(\n");
        ctx.rtl_inst.push_back("  .DataWidth(" + data_width_str + "),\n");
        ctx.rtl_inst.push_back("  .AddrWidth(" + addr_width_str + "),\n");
        ctx.rtl_inst.push_back("  .ReadPorts(" + read_ports_str + "),\n");
        ctx.rtl_inst.push_back("  .ReadMemHPath(\"" + init_path_escaped + "\")\n");
        ctx.rtl_inst.push_back(") " + rom_name + "__inst (\n");
        ctx.rtl_inst.push_back("  .clk(clk),\n");
        ctx.rtl_inst.push_back("  .rstn(rstn),\n");
        ctx.rtl_inst.push_back("  .s1_readreq(" + port_s1_readreq + "),\n");
        ctx.rtl_inst.push_back("  .s1_readaddr(" + port_s1_readaddr + "),\n");
        ctx.rtl_inst.push_back("  .s2_readdata(" + port_s2_readdata + ")\n");
        ctx.rtl_inst.push_back(");\n");
    }

}


} // namespace detail

} // namespace rtlgen
