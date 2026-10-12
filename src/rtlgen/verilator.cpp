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

#include "rtlgen.h"
#include "common.hpp"
#include "simgen.h"
#include "port_shape.h"
#include "apiinline/utils.hpp"
#include <algorithm>

namespace rtlgen {
using namespace detail;

namespace {
static string verilatorTopExpr(const string &port_name) {
    string member_name;
    member_name.reserve(port_name.size() * 2);
    for (size_t i = 0; i < port_name.size();) {
        if (i + 1 < port_name.size() && port_name[i] == '_' && port_name[i + 1] == '_') {
            member_name += "___05F";
            i += 2;
        } else {
            member_name.push_back(port_name[i]);
            ++i;
        }
    }
    return "top->" + member_name;
}

static string verilatorIndexedTopExpr(const string &port_name, bool is_arrayed, uint32_t idx) {
    if (is_arrayed) {
        return verilatorTopExpr(port_name) + "[" + std::to_string(idx) + "]";
    }
    return verilatorTopExpr(port_name);
}

static void emitVerilatorUnpackField(vector<string> &out, const FlatField &field, const string &src, const string &indent) {
    if (field.is_fixint) {
        for (uint32_t bit = 0; bit < field.width; bit += 64) {
            uint32_t size = std::min(64u, field.width-bit);
            out.push_back(indent + field.name + ".template pick<" + std::to_string(size) + ">(" + std::to_string(bit) + ") = static_cast<uint64_t>(__vul_get_bits(" + src + ", " + std::to_string(field.offset+bit) + ", " + std::to_string(size) + "));\n");
        }
    } else {
        out.push_back(indent + field.name + " = static_cast<std::remove_cvref_t<decltype(" + field.name + ")>>(__vul_get_bits(" + src + ", " + std::to_string(field.offset) + ", " + std::to_string(field.width) + "));\n");
    }
}

static void emitVerilatorUnpack(
    vector<string> &out,
    const ArgPort &port,
    const string &src_expr,
    const string &indent,
    bool declare_value = true
) {
    if (declare_value) {
        out.push_back(indent + port.type.toString() + " " + port.name + " = {};\n");
    }
    for (const auto &field : port.flat_fields) emitVerilatorUnpackField(out, field, src_expr, indent);
}

static void emitVerilatorPack(
    vector<string> &out,
    const ArgPort &port,
    const string &dst_expr,
    const string &value_root,
    const string &indent
) {
    for (const auto &field : port.flat_fields) {
        string value = flatFieldValueExpr(value_root, field.name);
        if (field.is_fixint) {
            for (uint32_t bit = 0; bit < field.width; bit += 64) {
                uint32_t size = std::min(64u, field.width-bit);
                out.push_back(indent + "__vul_set_bits(" + dst_expr + ", " + std::to_string(field.offset+bit) + ", " + std::to_string(size) + ", static_cast<uint64_t>(" + value + ".template pick<" + std::to_string(size) + ">(" + std::to_string(bit) + ")));\n");
            }
        } else out.push_back(indent + "__vul_set_bits(" + dst_expr + ", " + std::to_string(field.offset) + ", " + std::to_string(field.width) + ", static_cast<__uint128_t>(" + value + "));\n");
    }
}

static void validateTestMainBindings(
    const VulStaticTestHarnessModule &test,
    const VulStaticModuleInstance &top,
    const VulStaticConfigLib &configlib
) {
    for (const auto &[name, temp_req] : test.requests) {
        auto top_serv = top.services.find(name);
        if (top_serv == top.services.end()) {
            throw VulException("TestMain REQUEST '" + name + "' not found in top module services");
        }
        VulStaticReqServ req = staticalizeReqServ(temp_req, configlib);
        if (!req.match(top_serv->second)) {
            throw VulException("TestMain REQUEST '" + name + "' signature mismatch with top module service");
        }
    }
    for (const auto &[name, temp_serv] : test.services) {
        auto top_req = top.requests.find(name);
        if (top_req == top.requests.end()) {
            throw VulException("TestMain SERVICE '" + name + "' not found in top module requests");
        }
        VulStaticReqServ serv = staticalizeReqServ(temp_serv, configlib);
        if (!serv.match(top_req->second)) {
            throw VulException("TestMain SERVICE '" + name + "' signature mismatch with top module request");
        }
    }
    for (const auto &[name, query] : test.queries) {
        auto top_query = top.queries.find(name);
        if (top_query == top.queries.end()) {
            throw VulException("TestMain QUERY '" + name + "' not found in top module queries");
        }
        if (!(query.ret_type == top_query->second.ret_type)) {
            throw VulException("TestMain QUERY '" + name + "' return type mismatch with top module query");
        }
    }
}

} // namespace

vector<string> genVerilatorTestMainCpp(
    const VulStaticProject &project,
    const string &top_verilator_class_name
) {
    if (!project.top_module_instance) {
        throw VulException("Cannot generate Verilator TestMain without a top module");
    }
    const auto &top_module = *project.top_module_instance;
    const auto &test = project.test_harness;
    validateTestMainBindings(test, top_module, project.global_configlib);

    VulStaticBundleLib bundlelib = project.global_bundlelib;
    for (const auto &local_bundle : top_module.local_bundles) {
        bool replaced = false;
        for (auto &bundle : bundlelib) {
            if (bundle.name == local_bundle.name) {
                bundle = local_bundle;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            bundlelib.push_back(local_bundle);
        }
    }

    const string top_class_name = top_verilator_class_name.empty() ? ("V" + top_module.simClassName()) : top_verilator_class_name;
    vector<string> out;

    out.push_back("#include <array>\n");
    out.push_back("#include <cassert>\n");
    out.push_back("#include <cstdint>\n");
    out.push_back("#include <cstdlib>\n");
    out.push_back("#include <type_traits>\n");
    out.push_back("\n");
    out.push_back("#include \"fixint.hpp\"\n");
    out.push_back("#include \"verilated.h\"\n");
    out.push_back("#include \"" + top_class_name + ".h\"\n");
    out.push_back("\n");
    out.push_back("using std::array;\n");
    out.push_back("using int8 = int8_t;\n");
    out.push_back("using uint8 = uint8_t;\n");
    out.push_back("using int16 = int16_t;\n");
    out.push_back("using uint16 = uint16_t;\n");
    out.push_back("using int32 = int32_t;\n");
    out.push_back("using uint32 = uint32_t;\n");
    out.push_back("using int64 = int64_t;\n");
    out.push_back("using uint64 = uint64_t;\n");
    out.push_back("using int128 = __int128_t;\n");
    out.push_back("using uint128 = __uint128_t;\n");
    out.push_back("using int128_t = __int128_t;\n");
    out.push_back("using uint128_t = __uint128_t;\n");
    out.push_back("\n");
    for (const auto &cfg_entry : project.global_configlib) {
        out.push_back("constexpr int64_t " + cfg_entry.first + " = " + std::to_string(cfg_entry.second) + ";\n");
    }
    if (!project.global_configlib.empty()) {
        out.push_back("\n");
    }
    for (const auto &bundle : bundlelib) {
        auto bundle_lines = simgen::genStaticBundle(bundle);
        out.insert(out.end(), bundle_lines.begin(), bundle_lines.end());
        out.push_back("\n");
    }
    for (const auto &line : project.global_helper_codes) {
        out.push_back(line.ends_with("\n") ? line : (line + "\n"));
    }
    if (!project.global_helper_codes.empty()) {
        out.push_back("\n");
    }
    for (const auto &include : test.includedHeaders) {
        out.push_back("#include <" + include + ">\n");
    }
    if (!test.includedHeaders.empty()) {
        out.push_back("\n");
    }

    out.push_back("template <typename T>\n");
    out.push_back("static inline typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, uint64_t>::type\n");
    out.push_back("__vul_port_word(const T &value, uint32_t word) {\n");
    out.push_back("  return word == 0 ? static_cast<uint64_t>(value) : 0;\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("template <typename T>\n");
    out.push_back("static inline typename std::enable_if<!(std::is_integral<T>::value || std::is_enum<T>::value), uint64_t>::type\n");
    out.push_back("__vul_port_word(const T &value, uint32_t word) {\n");
    out.push_back("  uint32_t index = word * 2;\n");
    out.push_back("  uint64_t result = index < value.size() ? value[index] : 0;\n");
    out.push_back("  if (index + 1 < value.size()) result |= static_cast<uint64_t>(value[index + 1]) << 32;\n");
    out.push_back("  return result;\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("template <typename T>\n");
    out.push_back("static inline typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, void>::type\n");
    out.push_back("__vul_port_set_word(T &value, uint32_t word, uint64_t data) {\n");
    out.push_back("  if (word == 0) value = static_cast<T>(data);\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("template <typename T>\n");
    out.push_back("static inline typename std::enable_if<!(std::is_integral<T>::value || std::is_enum<T>::value), void>::type\n");
    out.push_back("__vul_port_set_word(T &value, uint32_t word, uint64_t data) {\n");
    out.push_back("  uint32_t index = word * 2;\n");
    out.push_back("  if (index < value.size()) value[index] = static_cast<uint32_t>(data);\n");
    out.push_back("  if (index + 1 < value.size()) value[index + 1] = static_cast<uint32_t>(data >> 32);\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("template <typename T>\n");
    out.push_back("static inline __uint128_t __vul_get_bits(const T &value, uint32_t offset, uint32_t width) {\n");
    out.push_back("  __uint128_t out = 0;\n");
    out.push_back("  for (uint32_t bit = 0; bit < width; ++bit) {\n");
    out.push_back("    uint32_t src = offset + bit;\n");
    out.push_back("    if ((__vul_port_word(value, src / 64) >> (src % 64)) & 1ULL) out |= ((__uint128_t)1 << bit);\n");
    out.push_back("  }\n");
    out.push_back("  return out;\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("template <typename T>\n");
    out.push_back("static inline void __vul_set_bits(T &value, uint32_t offset, uint32_t width, __uint128_t data) {\n");
    out.push_back("  for (uint32_t bit = 0; bit < width; ++bit) {\n");
    out.push_back("    uint32_t dst = offset + bit;\n");
    out.push_back("    uint32_t word = dst / 64;\n");
    out.push_back("    uint32_t shift = dst % 64;\n");
    out.push_back("    uint64_t cur = __vul_port_word(value, word);\n");
    out.push_back("    if ((data >> bit) & 1) cur |= (1ULL << shift); else cur &= ~(1ULL << shift);\n");
    out.push_back("    __vul_port_set_word(value, word, cur);\n");
    out.push_back("  }\n");
    out.push_back("}\n");
    out.push_back("\n");
    out.push_back("class VulTestMain {\n");
    out.push_back("public:\n");
    out.push_back("  VulTestMain() {\n");
    out.push_back("    top = new " + top_class_name + ";\n");
    out.push_back("    top->clk = 0;\n");
    out.push_back("    top->rstn = 0;\n");
    for (const auto &[name, top_serv] : top_module.services) {
        if (test.requests.find(name) != test.requests.end()) {
            if (top_serv.is_arrayed) {
                for (uint32_t idx = 0; idx < static_cast<uint32_t>(top_serv.array_size); ++idx) {
                    out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), true, idx) + " = false;\n");
                }
            } else {
                out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), false, 0) + " = false;\n");
            }
        }
    }
    out.push_back("    for (int i = 0; i < 4; ++i) sim_commit_raw();\n");
    out.push_back("    top->rstn = 1;\n");
    out.push_back("    eval_and_process_services();\n");
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  ~VulTestMain() {\n");
    out.push_back("    delete top;\n");
    out.push_back("  }\n");
    out.push_back("\n");
    for (const auto &[name, top_serv] : top_module.services) {
        auto req_iter = test.requests.find(name);
        if (req_iter == test.requests.end()) {
            continue;
        }
        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;
        for (const auto &arg : top_serv.args) arg_ports.push_back(procArg(arg, bundlelib));
        for (const auto &ret : top_serv.rets) ret_ports.push_back(procArg(ret, bundlelib));
        const bool is_arrayed = top_serv.is_arrayed;
        const uint32_t array_size = static_cast<uint32_t>(top_serv.array_size);
        const uint32_t emit_count = is_arrayed ? array_size : 1;
        if (is_arrayed) out.push_back("  template<uint32_t IDX>\n");
        auto method = top_serv;
        if (is_arrayed) method.name = portFlatName(name);
        out.push_back("  " + method.signatureFull() + " {\n");
        if (is_arrayed) out.push_back("    static_assert(IDX < " + std::to_string(array_size) + ", \"Request index out of range\");\n");
        for (uint32_t idx = 0; idx < emit_count; ++idx) {
            if (is_arrayed) out.push_back("    if constexpr (IDX == " + std::to_string(idx) + ") {\n");
            string branch = is_arrayed ? "      " : "    ";
            const string issued = is_arrayed ? ("__issued_" + name + "[" + std::to_string(idx) + "]") : ("__issued_" + name);
            out.push_back(branch + "assert(!" + issued + " && \"TestMain request '" + name + "' called more than once before sim_nextcycle()\");\n");
            out.push_back(branch + issued + " = true;\n");
            out.push_back(branch + verilatorIndexedTopExpr(reqservVldPort(name), is_arrayed, idx) + " = true;\n");
            for (const auto &arg : arg_ports) {
                emitVerilatorPack(out, arg, verilatorIndexedTopExpr(reqservArgPort(name, arg.name), is_arrayed, idx), arg.name, branch);
            }
            out.push_back(branch + "eval_and_process_services();\n");
            for (const auto &ret : ret_ports) {
                emitVerilatorUnpack(out, ret, verilatorIndexedTopExpr(reqservArgPort(name, ret.name), is_arrayed, idx), branch, false);
            }
            if (top_serv.has_handshake) {
                out.push_back(branch + "return " + verilatorIndexedTopExpr(reqservRdyPort(name), is_arrayed, idx) + ";\n");
            } else {
                out.push_back(branch + "return;\n");
            }
            if (is_arrayed) out.push_back("    }\n");
            if (!is_arrayed) break;
        }
        out.push_back("  }\n");
        out.push_back(portRequestFacade(top_serv));
        out.push_back("\n");
    }

    for (const auto &[name, query] : test.queries) {
        uint32_t width = 0;
        vector<FlatField> flat_fields;
        flatten_type_signature(query.ret_type, bundlelib, "value", width, flat_fields);
        out.push_back("  " + query.ret_type.toString() + " " + name + "() {\n");
        // QUERY observes combinational outputs without consuming next-cycle requests.
        out.push_back("    top->eval();\n");
        out.push_back("    " + query.ret_type.toString() + " value = " +
                      apiinline::defaultValueExprForType(query.ret_type, bundlelib) + ";\n");
        for (const auto &field : flat_fields)
            emitVerilatorUnpackField(out, field, verilatorTopExpr(queryPort(name)), "    ");
        out.push_back("    return value;\n");
        out.push_back("  }\n");
        out.push_back("\n");
    }

    out.push_back("protected:\n");
    for (const auto &line : test.globalCodes) {
        out.push_back("  " + line);
        if (line.empty() || line.back() != '\n') {
            out.push_back("\n");
        }
    }
    if (!test.globalCodes.empty()) {
        out.push_back("\n");
    }
    out.push_back("  " + top_class_name + " *top = nullptr;\n");
    out.push_back("  bool __processing_services = false;\n");
    for (const auto &[name, top_serv] : top_module.services) {
        if (test.requests.find(name) != test.requests.end()) {
            if (top_serv.is_arrayed) {
                out.push_back("  std::array<bool, " + std::to_string(top_serv.array_size) + "> __issued_" + name + " = {};\n");
            } else {
                out.push_back("  bool __issued_" + name + " = false;\n");
            }
        }
    }
    for (const auto &name : test.service_order) {
        const auto &temp_serv = test.services.at(name);
        const auto &top_req = top_module.requests.at(name);
        if (top_req.is_arrayed) {
            out.push_back("  std::array<bool, " + std::to_string(top_req.array_size) + "> __handled_" + name + " = {};\n");
        } else {
            out.push_back("  bool __handled_" + name + " = false;\n");
        }
    }
    if (!test.services.empty()) {
        out.push_back("\n");
    }

    out.push_back("  void sim_reset() {\n");
    out.push_back("    top->rstn = 0;\n");
    for (const auto &[name, top_serv] : top_module.services) {
        if (test.requests.find(name) == test.requests.end()) continue;
        if (top_serv.is_arrayed) {
            for (uint32_t idx = 0; idx < static_cast<uint32_t>(top_serv.array_size); ++idx) {
                out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), true, idx) + " = false;\n");
            }
            out.push_back("    __issued_" + name + ".fill(false);\n");
        } else {
            out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), false, 0) + " = false;\n");
            out.push_back("    __issued_" + name + " = false;\n");
        }
    }
    for (const auto &[name, temp_serv] : test.services) {
        const auto &top_req = top_module.requests.at(name);
        out.push_back("    __handled_" + name + (top_req.is_arrayed ? ".fill(false);\n" : " = false;\n"));
    }
    out.push_back("    for (int i = 0; i < 4; ++i) sim_commit_raw();\n");
    out.push_back("    top->rstn = 1;\n");
    out.push_back("    eval_and_process_services();\n");
    out.push_back("  }\n");
    out.push_back("\n");

    out.push_back("  void sim_execute() {\n");
    out.push_back("    eval_and_process_services();\n");
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  void sim_nextcycle() {\n");
    out.push_back("    sim_execute();\n");
    out.push_back("    sim_commit();\n");
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  void sim_commit() {\n");
    out.push_back("    top->clk = !top->clk;\n");
    out.push_back("    top->eval();\n");
    for (const auto &[name, top_serv] : top_module.services) {
        if (test.requests.find(name) != test.requests.end()) {
            if (top_serv.is_arrayed) {
                for (uint32_t idx = 0; idx < static_cast<uint32_t>(top_serv.array_size); ++idx) {
                    out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), true, idx) + " = false;\n");
                    out.push_back("    __issued_" + name + "[" + std::to_string(idx) + "] = false;\n");
                }
            } else {
                out.push_back("    " + verilatorIndexedTopExpr(reqservVldPort(name), false, 0) + " = false;\n");
                out.push_back("    __issued_" + name + " = false;\n");
            }
        }
    }
    out.push_back("    top->clk = !top->clk;\n");
    out.push_back("    top->eval();\n");
    for (const auto &[name, temp_serv] : test.services) {
        const auto &top_req = top_module.requests.at(name);
        if (top_req.is_arrayed) {
            out.push_back("    __handled_" + name + ".fill(false);\n");
        } else {
            out.push_back("    __handled_" + name + " = false;\n");
        }
    }
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  void sim_commit_raw() {\n");
    out.push_back("    top->clk = !top->clk;\n");
    out.push_back("    top->eval();\n");
    out.push_back("    top->clk = !top->clk;\n");
    out.push_back("    top->eval();\n");
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  void eval_and_process_services() {\n");
    out.push_back("    top->eval();\n");
    out.push_back("    process_services();\n");
    out.push_back("  }\n");
    out.push_back("\n");
    out.push_back("  void process_services() {\n");
    out.push_back("    if (__processing_services) return;\n");
    out.push_back("    __processing_services = true;\n");
    out.push_back("    bool again = true;\n");
    out.push_back("    while (again) {\n");
    out.push_back("      again = false;\n");

    for (const auto &name : test.service_order) {
        const auto &temp_serv = test.services.at(name);
        const auto &top_req = top_module.requests.at(name);
        VulStaticReqServ serv = staticalizeReqServ(temp_serv, project.global_configlib);
        vector<ArgPort> arg_ports;
        vector<ArgPort> ret_ports;
        for (const auto &arg : top_req.args) arg_ports.push_back(procArg(arg, bundlelib));
        for (const auto &ret : top_req.rets) ret_ports.push_back(procArg(ret, bundlelib));
        const bool is_arrayed = top_req.is_arrayed;
        const uint32_t array_size = static_cast<uint32_t>(top_req.array_size);
        const uint32_t emit_count = is_arrayed ? array_size : 1;
        for (uint32_t idx = 0; idx < emit_count; ++idx) {
            const string handled = is_arrayed ? ("__handled_" + name + "[" + std::to_string(idx) + "]") : ("__handled_" + name);
            const string valid = verilatorIndexedTopExpr(reqservVldPort(name), is_arrayed, idx);
            out.push_back("      {\n");
            for (const auto &arg : arg_ports) {
                emitVerilatorUnpack(out, arg, verilatorIndexedTopExpr(reqservArgPort(name, arg.name), is_arrayed, idx), "        ");
            }
            for (const auto &ret : ret_ports) {
                out.push_back("        " + ret.type.toString() + " " + ret.name + " = {};\n");
            }
            const string call_names = top_req.signatureArgNameList();
            if (serv.has_handshake) {
                out.push_back("        bool ready = __cond_" + name + (is_arrayed ? "<" + std::to_string(idx) + ">" : "") + "(" + call_names + ");\n");
                out.push_back("        " + verilatorIndexedTopExpr(reqservRdyPort(name), is_arrayed, idx) + " = ready;\n");
                out.push_back("        if (" + valid + " && ready && !" + handled + ") {\n");
            } else {
                out.push_back("        if (" + valid + " && !" + handled + ") {\n");
            }
            out.push_back("          __impl_" + name + (is_arrayed ? "<" + std::to_string(idx) + ">" : "") + "(" + call_names + ");\n");
            for (const auto &ret : ret_ports) {
                emitVerilatorPack(out, ret, verilatorIndexedTopExpr(reqservArgPort(name, ret.name), is_arrayed, idx), ret.name, "          ");
            }
            out.push_back("          " + handled + " = true;\n");
            out.push_back("          top->eval();\n");
            out.push_back("          again = true;\n");
            out.push_back("        }\n");
            out.push_back("      }\n");
        }
    }
    out.push_back("    }\n");
    out.push_back("    __processing_services = false;\n");
    out.push_back("  }\n");
    out.push_back("\n");

    for (const auto &[name, temp_serv] : test.services) {
        const auto &top_req = top_module.requests.at(name);
        VulStaticReqServ serv = staticalizeReqServ(temp_serv, project.global_configlib);
        if (top_req.is_arrayed) out.push_back("  template<uint32_t IDX>\n");
        out.push_back("  void __impl_" + name + "(" + top_req.signatureArgOnly() + ") {\n");
        for (const auto &line : lowerPortIndexDims(temp_serv.codelines, top_req.port_dims, project.global_configlib)) {
            out.push_back("    " + line);
            if (line.empty() || line.back() != '\n') out.push_back("\n");
        }
        out.push_back("  }\n");
        if (serv.has_handshake) {
            out.push_back("\n");
            if (top_req.is_arrayed) out.push_back("  template<uint32_t IDX>\n");
            out.push_back("  bool __cond_" + name + "(" + top_req.signatureArgOnly() + ") {\n");
            out.push_back("    return (" + lowerPortIndexDims(temp_serv.cond, top_req.port_dims, project.global_configlib) + ");\n");
            out.push_back("  }\n");
        }
        out.push_back("\n");
    }

    out.push_back("public:\n");
    out.push_back("  void simulation() {\n");
    for (const auto &line : test.test_codelines) {
        out.push_back("    " + line);
        if (line.empty() || line.back() != '\n') {
            out.push_back("\n");
        }
    }
    out.push_back("  }\n");
    out.push_back("\n");

    out.push_back("};\n");
    out.push_back("\n");
    out.push_back("int main(int argc, char **argv) {\n");
    out.push_back("  Verilated::commandArgs(argc, argv);\n");
    out.push_back("  VulTestMain test_main;\n");
    out.push_back("  test_main.simulation();\n");
    out.push_back("  return 0;\n");
    out.push_back("}\n");

    return out;

}

} // namespace rtlgen
