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
#include "debugmap.hpp"
#include <cctype>

namespace rtlgen {
namespace detail {

namespace {
bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}
vector<string> substituteConfigConstants(
    const vector<string> &lines,
    const VulStaticConfigLib &configlib
) {
    vector<string> result;
    result.reserve(lines.size());
    for (const auto &line : lines) {
        string out;
        for (size_t i = 0; i < line.size();) {
            if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
                size_t j = i + 1;
                while (j < line.size() && isIdentifierChar(line[j])) {
                    ++j;
                }
                string ident = line.substr(i, j - i);
                auto it = configlib.find(ident);
                if (it != configlib.end()) {
                    const bool left_ok = (i == 0 || !isIdentifierChar(line[i - 1]));
                    const bool right_ok = (j == line.size() || !isIdentifierChar(line[j]));
                    if (left_ok && right_ok) {
                        out += std::to_string(it->second);
                    } else {
                        out += ident;
                    }
                } else {
                    out += ident;
                }
                i = j;
            } else {
                out.push_back(line[i]);
                ++i;
            }
        }
        result.push_back(std::move(out));
    }
    return result;
}

} // namespace
void _procRegisters(RTLGenContext &ctx) {

    for (const auto &reg : ctx.module.registers) {
        VulErrorContextGuard reg_guard("processing register " + reg.name);
        const auto usage_it = ctx.register_api_usage.find(reg.name);
        const bool uses_hold = usage_it != ctx.register_api_usage.end() && usage_it->second.hold;
        const bool uses_reset = usage_it != ctx.register_api_usage.end() && usage_it->second.reset;

        if (pipelineOutput(reg.role)) {
            uint32_t width = 0;
            vector<FlatField> fields;
            flatten_type_signature(reg.signature, ctx.local_bundlelib, "value", width, fields);
            const string range = "[" + std::to_string(width - 1) + ":0] ";
            const string dims = reg.dims.empty() ? "" : "[" + std::to_string(reg.dims[0]) + "]";
            ctx.rtl_decl.push_back("reg " + range + ("__vul_pipeline_q_" + reg.name) + dims + (reg.role == VulRegisterRole::OutputValid ? " = 1'b0" : "") + ";\n");
            ctx.rtl_decl.push_back("wire " + range + "__vul_pipeline_d_" + reg.name + dims + ";\n");
            ctx.rtl_logic.push_back("always @(posedge clk) begin\n");
            ctx.rtl_logic.push_back("  if (!rstn) begin\n");
            int reset_count = reg.dims.empty() ? 1 : static_cast<int>(reg.dims[0]);
            for (int i=0; i<reset_count; ++i) ctx.rtl_logic.push_back("    __vul_pipeline_q_" + reg.name + (reg.dims.empty() ? "" : "[" + std::to_string(i) + "]") + " <= __vul_pipeline_reset_" + reg.name + "_" + std::to_string(i) + ";\n");
            ctx.rtl_logic.push_back("  end else begin\n");
            if (reg.dims.empty()) ctx.rtl_logic.push_back("  __vul_pipeline_q_" + reg.name + " <= __vul_pipeline_d_" + reg.name + ";\n");
            else ctx.rtl_logic.push_back("  for (int i = 0; i < " + std::to_string(reg.dims[0]) + "; ++i) __vul_pipeline_q_" + reg.name + "[i] <= __vul_pipeline_d_" + reg.name + "[i];\n");
            ctx.rtl_logic.push_back("  end\nend\n");
            string type = "Int<" + std::to_string(width) + ">";
            if (!reg.dims.empty()) type = "std::array<" + type + ", " + std::to_string(reg.dims[0]) + ">";
            ctx.hls_arguments.push_back("const " + type + " &rdata_" + reg.name + "__");
            ctx.rtl_logicports.push_back(".rdata_" + reg.name + "__(__vul_pipeline_q_" + reg.name + ")");
            continue;
        }

        bool is_ported = (reg.ports > 1);
        bool is_array = (!reg.dims.empty());

        string proxy_class_name = "__RegProxy_" + reg.signature.type;
        if (is_ported) {
            proxy_class_name += "__P" + std::to_string(reg.ports);
        }
        if (is_array) {
            proxy_class_name += "__D" + std::to_string(reg.dims[0]);
        }
        proxy_class_name += "__" + reg.name;

        uint32_t element_width = 0;
        std::vector<FlatField> out;
        flatten_type_signature(reg.signature, ctx.local_bundlelib, "value", element_width, out);
        uint32_t wrport_num = reg.ports;
        uint32_t size = (reg.dims.empty()) ? 1 : reg.dims[0];

        string ewid_str = std::to_string(element_width);
        string ewid_m1_str = std::to_string(element_width - 1);
        string wrport_num_str = std::to_string(wrport_num);
        string size_str = std::to_string(size);

        string element_type_str = reg.signature.toString();

        string rdata_type_str = "Int<" + ewid_str + ">";
        string wdata_type_str = "Int<" + ewid_str + ">";
        string wen_type_str = "bool";
        string ctrl_type_str = "bool";
        if (is_ported) {
            wdata_type_str = "std::array<" + wdata_type_str + ", " + wrport_num_str + ">";
            wen_type_str = "std::array<bool, " + wrport_num_str + ">";
        }
        if (is_array) {
            rdata_type_str = "std::array<" + rdata_type_str + ", " + size_str + ">";
            wdata_type_str = "std::array<" + wdata_type_str + ", " + size_str + ">";
            wen_type_str = "std::array<" + wen_type_str + ", " + size_str + ">";
            ctrl_type_str = "std::array<bool, " + size_str + ">";
        }
        string resetvalue_type_str = rdata_type_str;







        string wdata_port_name = "wdata_" + reg.name + "__";
        string wen_port_name = "wen_" + reg.name + "__";
        string rdata_port_name = "rdata_" + reg.name + "__";
        string holdnext_port_name = "holdnext_" + reg.name + "__";
        string resetnext_port_name = "resetnext_" + reg.name + "__";
        string resetvalue_port_name = "resetvalue_" + reg.name + "__";

        if (!pipelineInput(reg.role)) ctx.hls_arguments.push_back("const " + rdata_type_str + " &" + rdata_port_name);
        ctx.hls_arguments.push_back(wen_type_str + " &" + wen_port_name);
        ctx.hls_arguments.push_back(wdata_type_str + " &" + wdata_port_name);
        if (uses_hold) ctx.hls_arguments.push_back(ctrl_type_str + " &" + holdnext_port_name);
        if (uses_reset) ctx.hls_arguments.push_back(ctrl_type_str + " &" + resetnext_port_name);
        ctx.hls_arguments.push_back(resetvalue_type_str + " &" + resetvalue_port_name);

        if (is_array && is_ported) {
            ctx.hls_init.push_back("for (uint32_t __vul_i = 0; __vul_i < " + size_str + "; ++__vul_i) {\n");
            if (uses_hold) ctx.hls_init.push_back("  " + holdnext_port_name + "[__vul_i] = false;\n");
            if (uses_reset) ctx.hls_init.push_back("  " + resetnext_port_name + "[__vul_i] = false;\n");
            ctx.hls_init.push_back("  for (uint32_t __vul_p = 0; __vul_p < " + wrport_num_str + "; ++__vul_p) {\n");
            ctx.hls_init.push_back("    " + wen_port_name + "[__vul_i][__vul_p] = false;\n");
            ctx.hls_init.push_back("    " + wdata_port_name + "[__vul_i][__vul_p] = 0;\n");
            ctx.hls_init.push_back("  }\n");
            ctx.hls_init.push_back("}\n");
        } else if (is_array) {
            ctx.hls_init.push_back("for (uint32_t __vul_i = 0; __vul_i < " + size_str + "; ++__vul_i) {\n");
            if (uses_hold) ctx.hls_init.push_back("  " + holdnext_port_name + "[__vul_i] = false;\n");
            if (uses_reset) ctx.hls_init.push_back("  " + resetnext_port_name + "[__vul_i] = false;\n");
            ctx.hls_init.push_back("  " + wen_port_name + "[__vul_i] = false;\n");
            ctx.hls_init.push_back("  " + wdata_port_name + "[__vul_i] = 0;\n");
            ctx.hls_init.push_back("}\n");
        } else if (is_ported) {
            if (uses_hold) ctx.hls_init.push_back(holdnext_port_name + " = false;\n");
            if (uses_reset) ctx.hls_init.push_back(resetnext_port_name + " = false;\n");
            ctx.hls_init.push_back("for (uint32_t __vul_p = 0; __vul_p < " + wrport_num_str + "; ++__vul_p) {\n");
            ctx.hls_init.push_back("  " + wen_port_name + "[__vul_p] = false;\n");
            ctx.hls_init.push_back("  " + wdata_port_name + "[__vul_p] = 0;\n");
            ctx.hls_init.push_back("}\n");
        } else {
            if (uses_hold) ctx.hls_init.push_back(holdnext_port_name + " = false;\n");
            if (uses_reset) ctx.hls_init.push_back(resetnext_port_name + " = false;\n");
            ctx.hls_init.push_back(wen_port_name + " = false;\n");
            ctx.hls_init.push_back(wdata_port_name + " = 0;\n");
        }
        ctx.hls_reset_init.push_back("{\n");
        ctx.hls_reset_init_debug.push_back({});
        if (is_array) {
            ctx.hls_reset_init.push_back("  std::array<" + element_type_str + ", " + size_str + "> " + reg.name + "{};\n");
        } else {
            ctx.hls_reset_init.push_back("  " + element_type_str + " " + reg.name + "{};\n");
        }
        ctx.hls_reset_init_debug.push_back({});
        vector<string> reset_codelines = substituteConfigConstants(reg.reset_codelines, ctx.local_configlib);
        vulDebugAppendLines(ctx.hls_reset_init, ctx.hls_reset_init_debug, reset_codelines, reg.reset_codelines_debug);
        if (is_array) {
            for (uint32_t i = 0; i < size; i++) {
                const string idx = std::to_string(i);
                const string flat_name = reg.name + "_flatten_" + idx;
                ctx.hls_reset_init.push_back("  Int<" + ewid_str + "> " + flat_name + " = 0;\n");
                ctx.hls_reset_init_debug.push_back({});
                for (const auto &field : out) {
                    ctx.hls_reset_init.push_back("  " + uintExtractExpr(flat_name, field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, flatFieldValueExpr(reg.name + "[" + idx + "]", field.name)) + ";\n");
                    ctx.hls_reset_init_debug.push_back({});
                }
                ctx.hls_reset_init.push_back("  " + resetvalue_port_name + "[" + idx + "] = " + flat_name + ";\n");
                ctx.hls_reset_init_debug.push_back({});
            }
        } else {
            const string flat_name = reg.name + "_flatten";
            ctx.hls_reset_init.push_back("  Int<" + ewid_str + "> " + flat_name + " = 0;\n");
            ctx.hls_reset_init_debug.push_back({});
            for (const auto &field : out) {
                ctx.hls_reset_init.push_back("  " + uintExtractExpr(flat_name, field.offset + field.width - 1, field.offset) + " = " + packFlatFieldExpr(field, flatFieldValueExpr(reg.name, field.name)) + ";\n");
                ctx.hls_reset_init_debug.push_back({});
            }
            ctx.hls_reset_init.push_back("  " + resetvalue_port_name + " = " + flat_name + ";\n");
            ctx.hls_reset_init_debug.push_back({});
        }
        ctx.hls_reset_init.push_back("}\n");
        ctx.hls_reset_init_debug.push_back({});



        // generate register instances
        string reg_decl_name = pipelineInput(reg.role) ? "__vul_pipeline_q_" + reg.name : reg.name;
        string wen_wire_name = "wen_" + reg.name + "__";
        string wdata_wire_name = "wdata_" + reg.name + "__";
        string holdnext_wire_name = "holdnext_" + reg.name + "__";
        string resetnext_wire_name = "resetnext_" + reg.name + "__";
        string resetvalue_wire_name = "resetvalue_" + reg.name + "__";

        if (!is_array) {
            ctx.rtl_decl.push_back("reg [" + ewid_m1_str + ":0] " + reg_decl_name + (reg.role == VulRegisterRole::InputValid ? " = 1\'b0" : "") + ";\n");
            if (uses_hold) ctx.rtl_decl.push_back("wire " + holdnext_wire_name + ";\n");
            if (uses_reset) ctx.rtl_decl.push_back("wire " + resetnext_wire_name + ";\n");
            ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + resetvalue_wire_name + ";\n");
            if (is_ported) {
                ctx.rtl_decl.push_back("wire " + wen_wire_name + "[" + wrport_num_str + "];\n");
                ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + wdata_wire_name + "[" + wrport_num_str + "];\n");
            } else {
                ctx.rtl_decl.push_back("wire " + wen_wire_name + ";\n");
                ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + wdata_wire_name + ";\n");
            }

            // generate always block for register update
            ctx.rtl_logic.push_back("always @(posedge clk) begin\n");
            ctx.rtl_logic.push_back("  if (rstn == 0) begin\n");
            ctx.rtl_logic.push_back("    " + reg_decl_name + " <= " + resetvalue_wire_name + ";\n");
            ctx.rtl_logic.push_back("  end else begin\n");
            if (uses_reset) {
                ctx.rtl_logic.push_back("    if (" + resetnext_wire_name + ") begin\n");
                ctx.rtl_logic.push_back("      " + reg_decl_name + " <= " + resetvalue_wire_name + ";\n");
                ctx.rtl_logic.push_back("    end else ");
            }
            if (uses_hold) {
                ctx.rtl_logic.push_back("if (" + holdnext_wire_name + ") begin\n");
                ctx.rtl_logic.push_back("      " + reg_decl_name + " <= " + reg_decl_name + ";\n");
                ctx.rtl_logic.push_back("    end else ");
            }
            ctx.rtl_logic.push_back("begin\n");
            if (!is_ported) {
                ctx.rtl_logic.push_back("    if (" + wen_wire_name + ") begin\n");
                ctx.rtl_logic.push_back("      " + reg_decl_name + " <= " + wdata_wire_name + ";\n");
                ctx.rtl_logic.push_back("    end\n");
            } else {
                for (uint32_t i = 0; i < wrport_num; i++) {
                    ctx.rtl_logic.push_back("    " + (i==0?string(""):string("else ")) + "if (" + wen_wire_name + "[" + std::to_string(i) + "]) begin\n");
                    ctx.rtl_logic.push_back("      " + reg_decl_name + " <= " + wdata_wire_name + "[" + std::to_string(i) + "];\n");
                    ctx.rtl_logic.push_back("    end\n");
                }
            }
            ctx.rtl_logic.push_back("    end\n");
            ctx.rtl_logic.push_back("  end\n");
            ctx.rtl_logic.push_back("end\n");
        } else {
            ctx.rtl_decl.push_back("reg [" + ewid_m1_str + ":0] " + reg_decl_name + "[" + size_str + "];\n");
            if (uses_hold) ctx.rtl_decl.push_back("wire " + holdnext_wire_name + "[" + size_str + "];\n");
            if (uses_reset) ctx.rtl_decl.push_back("wire " + resetnext_wire_name + "[" + size_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + resetvalue_wire_name + "[" + size_str + "];\n");
            if (is_ported) {
                ctx.rtl_decl.push_back("wire " + wen_wire_name + "[" + size_str + "][" + wrport_num_str + "];\n");
                ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + wdata_wire_name + "[" + size_str + "][" + wrport_num_str + "];\n");
            } else {
                ctx.rtl_decl.push_back("wire " + wen_wire_name + "[" + size_str + "];\n");
                ctx.rtl_decl.push_back("wire [" + ewid_m1_str + ":0] " + wdata_wire_name + "[" + size_str + "];\n");
            }

            // generate always block for register update
            ctx.rtl_logic.push_back("always @(posedge clk) begin\n");
            ctx.rtl_logic.push_back("  if (rstn == 0) begin\n");
            ctx.rtl_logic.push_back("    for (int i = 0; i < " + size_str + "; i++) begin\n");
            ctx.rtl_logic.push_back("      " + reg_decl_name + "[i] <= " + resetvalue_wire_name + "[i];\n");
            ctx.rtl_logic.push_back("    end\n");
            ctx.rtl_logic.push_back("  end else begin\n");
            ctx.rtl_logic.push_back("    for (int i = 0; i < " + size_str + "; i++) begin\n");
            if (uses_reset) {
                ctx.rtl_logic.push_back("      if (" + resetnext_wire_name + "[i]) begin\n");
                ctx.rtl_logic.push_back("        " + reg_decl_name + "[i] <= " + resetvalue_wire_name + "[i];\n");
                ctx.rtl_logic.push_back("      end else ");
            }
            if (uses_hold) {
                ctx.rtl_logic.push_back("if (" + holdnext_wire_name + "[i]) begin\n");
                ctx.rtl_logic.push_back("        " + reg_decl_name + "[i] <= " + reg_decl_name + "[i];\n");
                ctx.rtl_logic.push_back("      end else ");
            }
            ctx.rtl_logic.push_back("begin\n");
            if (!is_ported) {
                ctx.rtl_logic.push_back("      if (" + wen_wire_name + "[i]) begin\n");
                ctx.rtl_logic.push_back("        " + reg_decl_name + "[i] <= " + wdata_wire_name + "[i];\n");
                ctx.rtl_logic.push_back("      end\n");
            } else {
                for (uint32_t j = 0; j < wrport_num; j++) {
                    ctx.rtl_logic.push_back("      " + (j==0?string(""):string("else ")) + "if (" + wen_wire_name + "[i][" + std::to_string(j) + "]) begin\n");
                    ctx.rtl_logic.push_back("        " + reg_decl_name + "[i] <= " + wdata_wire_name + "[i][" + std::to_string(j) + "];\n");
                    ctx.rtl_logic.push_back("      end\n");
                }
            }
            ctx.rtl_logic.push_back("      end\n");
            ctx.rtl_logic.push_back("    end\n");
            ctx.rtl_logic.push_back("  end\n");
            ctx.rtl_logic.push_back("end\n");
        }

        if (!pipelineInput(reg.role)) ctx.rtl_logicports.push_back("." + rdata_port_name + "(" + reg_decl_name + ")");
        ctx.rtl_logicports.push_back("." + wen_port_name + "(" + wen_wire_name + ")");
        ctx.rtl_logicports.push_back("." + wdata_port_name + "(" + wdata_wire_name + ")");
        if (uses_hold) ctx.rtl_logicports.push_back("." + holdnext_port_name + "(" + holdnext_wire_name + ")");
        if (uses_reset) ctx.rtl_logicports.push_back("." + resetnext_port_name + "(" + resetnext_wire_name + ")");
        ctx.rtl_logicports.push_back("." + resetvalue_port_name + "(" + resetvalue_wire_name + ")");

    }

};


} // namespace detail

} // namespace rtlgen
