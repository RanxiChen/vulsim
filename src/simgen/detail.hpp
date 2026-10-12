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

#pragma once

#include "simgen.h"
#include "instance_graph.h"

namespace simgen::detail {

inline const string CodeTab = "    ";

inline const string TickFunctionName = "on_current_tick";
inline const string ApplyTickFunctionName = "apply_next_tick";

inline const string RegisterClassName = "VulRegister";
inline const string RegisterArrayClassName = "VulRegisterArray";
inline const string BlockRAMClassName = "VulBRAM";
inline const string BlockRAM1RWClassName = "VulBRAM1RW";
inline const string ROMClassName = "VulROM";
inline const string QueueClassName = "VulQueue";
inline const string QueueMPClassName = "VulQueueMP";


struct ModuleGenContext {
    const VulStaticModuleInstance mod;
    const vector<VulTracedSignal> &traced_signals;
    const string mod_class_name;
    vector<string> decl_include_field;
    vector<string> decl_public_field;
    vector<string> decl_private_field;
    VulDebugLocs decl_private_field_debug;

    vector<string> impl_field;
    VulDebugLocs impl_field_debug;
    vector<string> impl_init_field;
    vector<string> impl_commit_field;
    VulDebugLocs impl_commit_field_debug;
    vector<string> impl_sys_reset_field;
    vector<string> impl_reg_reset_value_field;
    VulDebugLocs impl_reg_reset_value_field_debug;
    vector<string> impl_reg_reset_field;
    VulDebugLocs impl_reg_reset_field_debug;

    vector<string> bram_resources_files;

    ModuleGenContext(const VulStaticModuleInstance &original, const vector<VulTracedSignal> &signals)
        : mod(concreteGenerationView(original)), traced_signals(signals),
          mod_class_name(mod.simConcreteClassName()) {}
};

vector<string> genHeaderPrelude();
string childPtrFieldName(const string &name, const vector<ConfigRealValue> &indices);
string childPtrFieldName(const VulStaticModuleInstance &parent, const string &name);
bool childIsArrayTemplate(const VulStaticModuleInstance &mod);
const VulStaticModuleInstance &findChildTemplateByName(const VulStaticModuleInstance &mod, const string &name);
string _genStaticMemberTypeStr(const VulStaticBundleMember &member);
vector<string> _genStaticBundle(const VulStaticBundle &bundle);
void emitLocalDeclarations(ModuleGenContext &ctx);
void emitTransactions(ModuleGenContext &ctx);
void emitResources(ModuleGenContext &ctx);
void emitHierarchy(ModuleGenContext &ctx);
void emitTicksAndHelpers(ModuleGenContext &ctx);
void emitTracing(ModuleGenContext &ctx);

} // namespace simgen::detail
