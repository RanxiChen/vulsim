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
#include "debugmap.hpp"

namespace simgen {
namespace detail {

void emitResources(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    auto &decl_private_field = ctx.decl_private_field;
    auto &impl_commit_field = ctx.impl_commit_field;
    auto &impl_commit_field_debug = ctx.impl_commit_field_debug;
    auto &impl_sys_reset_field = ctx.impl_sys_reset_field;
    auto &impl_reg_reset_value_field = ctx.impl_reg_reset_value_field;
    auto &impl_reg_reset_value_field_debug = ctx.impl_reg_reset_value_field_debug;
    auto &impl_reg_reset_field = ctx.impl_reg_reset_field;
    auto &impl_reg_reset_field_debug = ctx.impl_reg_reset_field_debug;
    auto &bram_resources_files = ctx.bram_resources_files;
    // generate register
    for (const auto &reg : mod.registers) {
        const string storage = reg.role == VulRegisterRole::Ordinary ? reg.name : "__vul_store_" + reg.name;
        const auto &sig = reg.signature;
        string type_str;
        string base_type = sig.type;
        if (sig.uint_length > 0) {
            base_type = std::string(UIntClassName) + "<" + std::to_string(sig.uint_length) + ">";
        }
        if (!reg.dims.empty()) {
            type_str += RegisterArrayClassName + "<" + base_type;
            for (const auto &dim : reg.dims) {
                type_str += "," + std::to_string(dim);
            }
            if (reg.ports > 1) {
                type_str = type_str + ", " + std::to_string(reg.ports);
            }
            type_str += ">";

            decl_private_field.push_back(type_str + " " + storage + ";\n");
            // decl_private_field.push_back("template <uint32_t P = 0>\n");
            // decl_private_field.push_back("void " + reg.name + "_setnext(const uint64_t idx, const " + base_type + " &value) {\n");
            // decl_private_field.push_back(CodeTab + reg.name + ".setnext<P>(idx, value);\n");
            // decl_private_field.push_back("}\n");
            decl_private_field.push_back("\n");
        } else {
            if (reg.ports > 1) {
                type_str = RegisterClassName + "<" + base_type + ", " + std::to_string(reg.ports) + ">";
            } else {
                type_str = RegisterClassName + "<" + base_type + ">";
            }

            decl_private_field.push_back(type_str + " " + storage + ";\n");
            // decl_private_field.push_back("template <uint32_t P = 0>\n");
            // decl_private_field.push_back("void " + reg.name + "_setnext(const " + base_type + " &value) {\n");
            // decl_private_field.push_back(CodeTab + reg.name + ".setnext<P>(value);\n");
            // decl_private_field.push_back("}\n");
            // decl_private_field.push_back("const " + base_type + " &" + reg.name + "_get() const {\n");
            // decl_private_field.push_back(CodeTab + "return " + reg.name + ".get();\n");
            // decl_private_field.push_back("}\n");
            decl_private_field.push_back("\n");
        }
        if (reg.role != VulRegisterRole::Ordinary) {
            string view = pipelineInput(reg.role) ? "vulstorage::PipelineWriteView<" + type_str + ">" : "vulstorage::PipelineReadView<" + base_type + ", " + type_str + ">";
            decl_private_field.push_back(view + " " + reg.name + "{" + storage + "};\n");
        }
        if (pipelineOutput(reg.role)) {
            impl_commit_field.push_back(storage + "." + ApplyTickFunctionName + "();\n");
            continue;
        }
        VulStaticBundleMember sig_as_member;
        sig_as_member.name = reg.name;
        sig_as_member.type = reg.signature;
        sig_as_member.dims = reg.dims;
        vulDebugAppendLine(impl_reg_reset_value_field, impl_reg_reset_value_field_debug, "{\n");
        vulDebugAppendLine(impl_reg_reset_value_field, impl_reg_reset_value_field_debug,
                           _genStaticMemberTypeStr(sig_as_member) + " " + reg.name + "{};\n");
        vulDebugAppendLines(impl_reg_reset_value_field, impl_reg_reset_value_field_debug, reg.reset_codelines, reg.reset_codelines_debug);
        vulDebugAppendLine(impl_reg_reset_value_field, impl_reg_reset_value_field_debug,
                           "this->" + storage + "._set_reset_value(" + reg.name + ");\n");
        vulDebugAppendLine(impl_reg_reset_value_field, impl_reg_reset_value_field_debug, "}\n");

        impl_reg_reset_field.push_back("this->" + storage + "._reset();\n");

        impl_commit_field.push_back(storage + "." + ApplyTickFunctionName + "();\n");
    }

    // generate wire
    for (const auto &wire : mod.wires) {
        const auto &sig = wire.signature;
        string type_str = sig.toString();

        decl_private_field.push_back(type_str + " " + wire.name + ";\n");
        decl_private_field.push_back("\n");

        impl_commit_field.push_back("{\n");
        vulDebugAppendLines(impl_commit_field, impl_commit_field_debug, wire.reset_codelines, wire.reset_codelines_debug);
        impl_commit_field.push_back("}\n");
        impl_reg_reset_field.push_back("{\n");
        vulDebugAppendLines(impl_reg_reset_field, impl_reg_reset_field_debug, wire.reset_codelines, wire.reset_codelines_debug);
        impl_reg_reset_field.push_back("}\n");
    }

    // generate bram
    for (const auto &bram : mod.brams) {
        VulErrorContextGuard context_guard("processing bram " + bram.name);
        string bram_class = "";
        if (bram.read_ports == 0 || bram.write_ports == 0) {
            bram_class = BlockRAM1RWClassName + "<" +
                bram.data_type.toString() + ", " +
                std::to_string(bram.addr_size) + ">";
        } else {
            bram_class = BlockRAMClassName + "<" +
            bram.data_type.toString() + ", " +
            std::to_string(bram.addr_size) + ", " +
            std::to_string(bram.read_ports) + ", " +
            std::to_string(bram.write_ports) + ">";
        }

        decl_private_field.push_back(bram_class + " " + bram.name + ";\n");
        impl_commit_field.push_back(CodeTab + bram.name + "." + ApplyTickFunctionName + "();\n");
    }
    // generate rom
    for (const auto &rom : mod.roms) {
        VulErrorContextGuard context_guard("processing rom " + rom.name);
        bram_resources_files.push_back(rom.init_path);
        string rom_class = ROMClassName + "<" +
            std::to_string(rom.data_width) + ", " +
            std::to_string(rom.addr_size) + ", " +
            std::to_string(rom.read_ports) + ">";

        decl_private_field.push_back(rom_class + " " + rom.name + "{\"" + rom.init_path + "\"};\n");
        impl_commit_field.push_back(CodeTab + rom.name + "." + ApplyTickFunctionName + "();\n");
    }

    // generate queues
    for (const auto &queue : mod.queues) {
        VulErrorContextGuard context_guard("processing queue " + queue.name);
        if (queue.depth == 0) {
            throw VulException("Queue must have positive depth");
        }
        if (queue.deq_width == 0 || queue.enq_width == 0) {
            throw VulException("Queue must have positive enq_width and deq_width");
        }
        bool is_multi_queue = queue.deq_width > 1 || queue.enq_width > 1;
        string queue_param = queue.type.toString() + ", " + std::to_string(queue.depth);
        if (is_multi_queue) {
            queue_param += ", " + std::to_string(queue.enq_width) + ", " + std::to_string(queue.deq_width);
        }
        string queue_class = (is_multi_queue ? QueueMPClassName : QueueClassName) + "<" + queue_param + ">";
        decl_private_field.push_back(queue_class + " " + queue.name + ";\n");
        impl_sys_reset_field.push_back(queue.name + " = " + queue_class + "{};\n");
        impl_commit_field.push_back(CodeTab + queue.name + "." + ApplyTickFunctionName + "();\n");
    }

}

} // namespace detail

} // namespace simgen
