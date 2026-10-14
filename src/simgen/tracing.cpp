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

namespace simgen {
namespace detail {

void emitTracing(ModuleGenContext &ctx) {
    const auto &mod = ctx.mod;
    const auto &traced_signals = ctx.traced_signals;
    auto &decl_private_field = ctx.decl_private_field;
    auto &impl_init_field = ctx.impl_init_field;
    auto &impl_commit_field = ctx.impl_commit_field;
    // trace
    if (!traced_signals.empty()) {
        for (uint64_t i = 0; i < traced_signals.size(); ++i) {
            const auto &sig = traced_signals[i];
            const bool needs_trace_bitmap = childIsArrayTemplate(mod) && !sig.trace_all_instances;
            string traceid_var = "_trace_id_" + std::to_string(i);
            string tracebitmap_var = "_trace_bitmap_" + std::to_string(i);
            string signal_name = mod.concatInstancePath(".") + "." + sig.signal_path;
            string access_path = "";
            string regname = sig.signal_path;
            const string &signal_path = sig.signal_path;
            const size_t first_dot_pos = signal_path.find('.');
            const size_t first_bracket_pos = signal_path.find('[');
            size_t regname_end_pos = signal_path.size();
            if (first_bracket_pos != string::npos && (first_dot_pos == string::npos || first_bracket_pos < first_dot_pos)) {
                regname_end_pos = first_bracket_pos;
            } else if (first_dot_pos != string::npos) {
                regname_end_pos = first_dot_pos;
            }
            regname = signal_path.substr(0, regname_end_pos);
            access_path = signal_path.substr(regname_end_pos);
            bool is_reg_array = false;
            for (const auto &reg : mod.registers) {
                if (reg.name == regname) {
                    is_reg_array = !reg.dims.empty();
                    if (reg.role != VulRegisterRole::Ordinary) regname = "__vul_store_" + regname;
                    break;
                }
            }
            if (!is_reg_array) {
                regname = regname + ".get()"; // .和[]运算符不能被隐式类型转换
            }
            access_path = regname + access_path;
            if (sig.bit_width > 64) {
                access_path = access_path + ".get_data()";
            } else if (sig.is_fixint) {
                access_path = "(" + access_path + ").template to<uint64_t>()";
            } else {
                access_path = "static_cast<uint64_t>(" + access_path + ")";
            }

            decl_private_field.push_back("uint32_t " + traceid_var + " = 0;\n");
            if (needs_trace_bitmap) {
                decl_private_field.push_back("bool " + tracebitmap_var + " = false;\n");
            }

            if (childIsArrayTemplate(mod)) {
                impl_init_field.push_back("{\n");
                if (!needs_trace_bitmap) {
                    impl_init_field.push_back(CodeTab + "std::string __trace_signal_name = \"" + mod.concatInstancePath(".") + "\";\n");
                    impl_init_field.push_back(CodeTab + "__trace_signal_name += \"." + sig.signal_path + "\";\n");
                    impl_init_field.push_back(CodeTab + traceid_var + " = trace_registe_signal(__trace_signal_name, " + std::to_string(sig.bit_width) + ");\n");
                } else {
                    impl_init_field.push_back(CodeTab + tracebitmap_var + " = false;\n");
                    for (const auto &filter : sig.instance_index_filters) {
                        string cond;
                        for (size_t dim = 0; dim < filter.size(); ++dim) {
                            if (!filter[dim].has_value()) continue;
                            if (!cond.empty()) cond += " && ";
                            cond += "__array_idx_" + std::to_string(dim) + " == " + std::to_string(*filter[dim]);
                        }
                        if (cond.empty()) {
                            impl_init_field.push_back(CodeTab + tracebitmap_var + " = true;\n");
                            break;
                        }
                        impl_init_field.push_back(CodeTab + "if (" + cond + ") " + tracebitmap_var + " = true;\n");
                    }
                    impl_init_field.push_back(CodeTab + "std::string __trace_signal_name = \"" + mod.concatInstancePath(".") + "\";\n");
                    impl_init_field.push_back(CodeTab + "__trace_signal_name += \"." + sig.signal_path + "\";\n");
                    impl_init_field.push_back(CodeTab + "if (" + tracebitmap_var + ") " + traceid_var + " = trace_registe_signal(__trace_signal_name, " + std::to_string(sig.bit_width) + ");\n");
                }
                impl_init_field.push_back("}\n");
            } else {
                impl_init_field.push_back(traceid_var + " = trace_registe_signal(\"" + signal_name + "\", " + std::to_string(sig.bit_width) + ");\n");
            }
            if (needs_trace_bitmap) {
                impl_commit_field.push_back("if (" + tracebitmap_var + ") trace_record(" + traceid_var + ", " + access_path + ");\n");
            } else {
                impl_commit_field.push_back("trace_record(" + traceid_var + ", " + access_path + ");\n");
            }
        }
    }

}

} // namespace detail

} // namespace simgen
