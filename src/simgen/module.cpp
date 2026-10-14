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
#include <algorithm>

namespace simgen {
using namespace detail;

namespace detail {

void emitLocalDeclarations(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    auto &decl_private_field = ctx.decl_private_field;
    // local params and consts
    for (const auto &param : mod.local_parameters) {
        auto bound = mod.parameter_coordinate_indices.find(param.first);
        const string value = bound == mod.parameter_coordinate_indices.end() ? std::to_string(param.second) : "__vul_idx_" + std::to_string(bound->second);
        decl_private_field.push_back("static constexpr int64_t " + param.first + " = " + value + ";\n");
    }
    for (const auto &constvar : mod.local_consts) {
        decl_private_field.push_back("static constexpr int64_t " + constvar.first + " = " + std::to_string(constvar.second) + ";\n");
    }
    // local bundles
    for (const auto &bundle_entry : mod.local_bundles) {
        auto bundle_lines = _genStaticBundle(bundle_entry);
        decl_private_field.insert(decl_private_field.end(), bundle_lines.begin(), bundle_lines.end());
        decl_private_field.push_back("\n");
    }

}

void emitTicksAndHelpers(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    const auto &mod_class_name = ctx.mod_class_name;
    auto &decl_private_field = ctx.decl_private_field;
    auto &decl_private_field_debug = ctx.decl_private_field_debug;
    auto &impl_field = ctx.impl_field;
    auto &impl_field_debug = ctx.impl_field_debug;
    // tick functions
    for (uint64_t i = 0; i < mod.tick_blocks.size(); i++) {
        const auto &tick = mod.tick_blocks[i];
        string tick_func_name = "__tick" + std::to_string(i);

        decl_private_field.push_back("void " + tick_func_name + "();\n");

        impl_field.push_back("void " + mod_class_name + "::" + tick_func_name + "() {\n");
        vulDebugAppendLines(impl_field, impl_field_debug, tick.codelines, tick.codelines_debug);
        impl_field.push_back("}\n");
        impl_field.push_back("\n");
    }

    for (const auto &pipe : mod.pipelines) {
        const string prefix = "__vul_pipeline_" + pipe.name;
        decl_private_field.push_back("struct " + prefix + "_scope {\n");
        vulDebugAppendLines(decl_private_field, decl_private_field_debug, mod.helper_codes, mod.helper_codes_debug);
        string signature = "void compute(";
        string arguments;
        string iv, ov;
        vector<const VulStaticRegister*> outputs;
        for (const auto &name : pipe.endpoints) {
            const auto it = std::find_if(mod.registers.begin(), mod.registers.end(), [&](const auto &r) { return r.name == name; });
            const auto &reg = *it;
            string type = reg.signature.toString();
            if (!reg.dims.empty()) type = "std::array<" + type + ", " + std::to_string(reg.dims[0]) + ">";
            if (reg.role == VulRegisterRole::OutputValid) { ov = name; outputs.push_back(&reg); continue; }
            if (!arguments.empty()) { arguments += ", "; signature += ", "; }
            signature += (pipelineInput(reg.role) ? "const " : "") + type + " &" + name;
            if (pipelineInput(reg.role)) {
                if (reg.dims.empty()) arguments += "__vul_store_" + name + ".get()";
                else {
                    string expression = type + "{";
                    for (int i = 0; i < reg.dims[0]; ++i) { if (i) expression += ", "; expression += "__vul_store_" + name + "[" + std::to_string(i) + "]"; }
                    arguments += expression + "}";
                }
                if (reg.role == VulRegisterRole::InputValid) iv = name;
            } else { arguments += prefix + "_result_" + name; outputs.push_back(&reg); }
        }
        decl_private_field.push_back(signature + ") {\n");
        vulDebugAppendLines(decl_private_field, decl_private_field_debug, pipe.codelines, pipe.codelines_debug);
        decl_private_field.push_back("}\n};\n");
        for (const auto *reg : outputs) {
            const string name = reg->name;
            string type = reg->signature.toString();
            if (!reg->dims.empty()) type = "std::array<" + type + ", " + std::to_string(reg->dims[0]) + ">";
            const string value = prefix + "_result_" + name;
            decl_private_field.push_back(type + " " + value + "{};\n");
            if (pipe.latency > 1) decl_private_field.push_back("std::array<" + type + ", " + std::to_string(pipe.latency - 1) + "> " + prefix + "_buffer_" + name + "{};\n");
        }
        decl_private_field.push_back("void " + prefix + "_execute() {\n");
        const string result_valid = ov.empty() ? "" : prefix + "_result_" + ov;
        const string valid_buffer = ov.empty() ? "" : prefix + "_buffer_" + ov;
        if (!ov.empty()) {
            decl_private_field.push_back(result_valid + " = __vul_store_" + iv + ".get();\n");
            decl_private_field.push_back("if (" + result_valid + ") {\n");
        }
        decl_private_field.push_back(prefix + "_scope{}.compute(" + arguments + ");\n");
        if (!ov.empty()) decl_private_field.push_back("}\n");
        const string output_valid = pipe.latency == 1 ? result_valid : valid_buffer + "[" + std::to_string(pipe.latency - 2) + "]";
        for (const auto *reg : outputs) {
            const string name = reg->name;
            const bool guarded = !ov.empty() && name != ov;
            const string value = pipe.latency == 1 ? prefix + "_result_" + name : prefix + "_buffer_" + name + "[" + std::to_string(pipe.latency - 2) + "]";
            if (guarded) decl_private_field.push_back("if (" + output_valid + ") {\n");
            if (reg->dims.empty()) decl_private_field.push_back("__vul_store_" + name + ".setnext(" + value + ");\n");
            else for (int i = 0; i < reg->dims[0]; ++i) decl_private_field.push_back("__vul_store_" + name + ".setnext(" + std::to_string(i) + ", " + value + "[" + std::to_string(i) + "]);\n");
            if (guarded) decl_private_field.push_back("}\n");
            if (pipe.latency > 1 && name != ov) {
                const string buffer = prefix + "_buffer_" + name;
                const string move_guard = guarded ? "if (" + valid_buffer + "[i-1]) " : "";
                const string insert_guard = guarded ? "if (" + result_valid + ") " : "";
                ctx.impl_commit_field.push_back("for (int i = " + std::to_string(pipe.latency - 2) + "; i > 0; --i) " + move_guard + buffer + "[i] = " + buffer + "[i-1];\n");
                ctx.impl_commit_field.push_back(insert_guard + buffer + "[0] = " + prefix + "_result_" + name + ";\n");
            }
        }
        // Move valid last so every data buffer observes the old source validity,
        // regardless of endpoint declaration order.
        if (pipe.latency > 1 && !ov.empty()) {
            ctx.impl_commit_field.push_back("for (int i = " + std::to_string(pipe.latency - 2) + "; i > 0; --i) " + valid_buffer + "[i] = " + valid_buffer + "[i-1];\n");
            ctx.impl_commit_field.push_back(valid_buffer + "[0] = " + result_valid + ";\n");
        }
        decl_private_field.push_back("}\n");
        decl_private_field.push_back("void " + prefix + "_reset() {\n");
        for (const auto *reg : outputs) decl_private_field.push_back(prefix + "_result_" + reg->name + " = {};\n");
        decl_private_field.push_back(prefix + "_scope{}.compute(" + arguments + ");\n");
        if (!ov.empty()) decl_private_field.push_back(prefix + "_result_" + ov + " = __vul_store_" + iv + ".get();\n");
        for (const auto *reg : outputs) {
            const string value = prefix + "_result_" + reg->name;
            const string storage = "__vul_store_" + reg->name;
            decl_private_field.push_back(storage + "._set_reset_value(" + value + ");\n" + storage + "._reset();\n");
            if (pipe.latency > 1) decl_private_field.push_back("for (auto &stage : " + prefix + "_buffer_" + reg->name + ") stage = " + value + ";\n");
        }
        decl_private_field.push_back("}\n");
        ctx.impl_reg_reset_field.push_back(prefix + "_reset();\n");
    }

    // helper field
    vulDebugAppendLines(decl_private_field, decl_private_field_debug, mod.helper_codes, mod.helper_codes_debug);

}

} // namespace detail

StaticModuleCodeHpp genStaticModuleCodeHpp(const VulStaticModuleInstance &original, const vector<VulTracedSignal> &traced_signals) {
    ModuleGenContext ctx(original, traced_signals);
    emitLocalDeclarations(ctx);
    emitTransactions(ctx);
    emitResources(ctx);
    emitHierarchy(ctx);
    emitTicksAndHelpers(ctx);
    emitTracing(ctx);

    const auto &mod = ctx.mod;
    const auto &mod_class_name = ctx.mod_class_name;
    auto &decl_include_field = ctx.decl_include_field;
    auto &decl_public_field = ctx.decl_public_field;
    auto &decl_private_field = ctx.decl_private_field;
    auto &decl_private_field_debug = ctx.decl_private_field_debug;
    auto &impl_field = ctx.impl_field;
    auto &impl_field_debug = ctx.impl_field_debug;
    auto &impl_init_field = ctx.impl_init_field;
    auto &impl_commit_field = ctx.impl_commit_field;
    auto &impl_commit_field_debug = ctx.impl_commit_field_debug;
    auto &impl_sys_reset_field = ctx.impl_sys_reset_field;
    auto &impl_reg_reset_value_field = ctx.impl_reg_reset_value_field;
    auto &impl_reg_reset_value_field_debug = ctx.impl_reg_reset_value_field_debug;
    auto &impl_reg_reset_field = ctx.impl_reg_reset_field;
    auto &impl_reg_reset_field_debug = ctx.impl_reg_reset_field_debug;
    auto &bram_resources_files = ctx.bram_resources_files;
    // preparation done, start generating code lines

    vector<string> decl = genHeaderPrelude();
    VulDebugLocs decl_debug;
    decl.push_back("#include \"common.h\"\n");
    decl.push_back("#include \"header.hpp\"\n");
    decl.push_back("#include \"vullib.h\"\n");
    decl.push_back("#include <stdexcept>\n");
    decl.push_back("\n");

    // include child module decls
    for (const auto &line : decl_include_field) {
        decl.push_back(line);
    }
    decl.push_back("\n");

    auto forward = [](const VulStaticModuleInstance &node) {
        const auto coords = node.coordinateContext();
        string out;
        if (!coords.empty()) {
            out += "template <";
            for (size_t i = 0; i < coords.size(); ++i) {
                if (i) out += ", ";
                out += "uint32_t __vul_idx_" + std::to_string(i);
            }
            out += "> ";
        }
        out += "class " + node.simClassName() + ";\n";
        if (!coords.empty()) out += "template <> class " + node.simConcreteClassName() + ";\n";
        return out;
    };
    decl.push_back(forward(*mod.parent));
    decl.push_back(forward(mod));
    decl.push_back((mod.coordinateContext().empty() ? "" : "template <> ") + string("class ") + mod_class_name + " {\n");
    decl.push_back("private:\n");
    decl.push_back(CodeTab + mod.parent->simConcreteClassName() + " * __parent;\n");
    const auto allcoords = mod.coordinateContext();
    for (size_t i = 0; i < allcoords.size(); ++i)
        decl.push_back("static constexpr uint32_t __vul_idx_" + std::to_string(i) + " = " + std::to_string(allcoords[i]) + ";\n");
    for (size_t i = 0; i < mod.instance_array_indices.size(); ++i)
        decl.push_back("static constexpr uint32_t __array_idx_" + std::to_string(i) + " = " + std::to_string(mod.instance_array_indices[i]) + ";\n");
    decl.push_back("\n");

    // entry declarations
    decl.push_back("public:\n");
    decl.push_back("void " + TickFunctionName + "();\n");
    decl.push_back("void " + ApplyTickFunctionName + "();\n");
    decl.push_back("void __sys_reset();\n");
    decl.push_back("void __init_reg_reset_values();\n");
    decl.push_back("void __reg_reset();\n");
    decl.push_back("void reset() { __sys_reset(); __reg_reset(); }\n");
    decl.push_back("void init();\n");
    decl.push_back("\n");

    // constructor declaration
    string ctor_sig = mod.simClassName() + "(" + mod.parent->simConcreteClassName() + " *parent";
    string ctor_init = "__parent(parent)";
    ctor_sig += ")";
    decl.push_back(ctor_sig + " : " + ctor_init + " {\n");
    decl.push_back(CodeTab + "init();\n");
    decl.push_back(CodeTab + "__init_reg_reset_values();\n");
    decl.push_back(CodeTab + "__reg_reset();\n");
    decl.push_back("}\n");
    decl.push_back("\n");

    // private member declaration
    decl.push_back("private:\n");
    vulDebugAppendLines(decl, decl_debug, decl_private_field, decl_private_field_debug);
    decl.push_back("\n");

    // public member declaration
    decl.push_back("public:\n");
    for (const auto &line : decl_public_field) {
        decl.push_back(line);
    }
    decl.push_back("\n");

    decl.push_back("};\n");
    decl.push_back("\n");



    vector<string> impl = genHeaderPrelude();
    VulDebugLocs impl_debug;

    impl.push_back("#include \"" + mod.simDeclPath() + "\"\n");
    impl.push_back("\n");

    // tick function implementations
    impl.push_back("void " + mod_class_name + "::" + TickFunctionName + "() {\n");
    for (const auto &id : mod.update_seq) {
        if (id == mod.instance_id) {
            for (const auto &pipe : mod.pipelines) impl.push_back(CodeTab + "__vul_pipeline_" + pipe.name + "_execute();\n");
            // tick functions here
            for (uint64_t i = 0; i < mod.tick_blocks.size(); i++) {
                impl.push_back(CodeTab + "__tick" + std::to_string(i) + "();\n");
            }
        } else {
            for (const auto &inst_ptr : mod.children) {
                if (inst_ptr->instance_id == id) {
                    impl.push_back(CodeTab + childPtrFieldName(inst_ptr->instance_path.back(), {}) + "->" + TickFunctionName + "();\n");
                    break;
                }
            }
        }
    }
    impl.push_back("}\n");
    impl.push_back("\n");

    // apply tick function implementations
    impl.push_back("void " + mod_class_name + "::" + ApplyTickFunctionName + "() {\n");
    vulDebugAppendLines(impl, impl_debug, impl_commit_field, impl_commit_field_debug);
    impl.push_back("}\n");

    // sys reset function implementations
    impl.push_back("void " + mod_class_name + "::__sys_reset() {\n");
    impl.insert(impl.end(), impl_sys_reset_field.begin(), impl_sys_reset_field.end());
    impl.push_back("}\n");
    impl.push_back("\n");

    // register reset value initialization function implementations
    impl.push_back("void " + mod_class_name + "::__init_reg_reset_values() {\n");
    vulDebugAppendLines(impl, impl_debug, impl_reg_reset_value_field, impl_reg_reset_value_field_debug);
    impl.push_back("}\n");
    impl.push_back("\n");

    // reg reset function implementations
    impl.push_back("void " + mod_class_name + "::__reg_reset() {\n");
    vulDebugAppendLines(impl, impl_debug, impl_reg_reset_field, impl_reg_reset_field_debug);
    impl.push_back("}\n");
    impl.push_back("\n");

    // init function implementations
    impl.push_back("void " + mod_class_name + "::init() {\n");
    impl.insert(impl.end(), impl_init_field.begin(), impl_init_field.end());
    impl.push_back("}\n");
    impl.push_back("\n");

    // other function implementations
    vulDebugAppendLines(impl, impl_debug, impl_field, impl_field_debug);

    vulDebugNormalize(decl, decl_debug);
    vulDebugNormalize(impl, impl_debug);
    return {decl, decl_debug, vulDebugBuildGeneratedMap(decl_debug), impl, impl_debug, vulDebugBuildGeneratedMap(impl_debug), bram_resources_files};
}

} // namespace simgen
