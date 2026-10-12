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
#include "toposort.hpp"

namespace simgen {
namespace detail {

string _genStaticMemberTypeStr(const VulStaticBundleMember &member) {
    string type_str;
    string base_type = member.type.toString();
    if (!member.dims.empty()) {
        for (const auto &dim : member.dims) {
            type_str += "std::array<";
        }
        type_str += base_type;
        for (uint32_t i = member.dims.size(); i > 0; i--) {
            type_str += ", " + std::to_string(member.dims[i-1]) + ">";
        }
    } else {
        type_str = base_type;
    }
    return type_str;
}

vector<string> _genStaticBundle(const VulStaticBundle &bundle) {
    vector<string> out_lines;

    if (bundle.is_alias) {
        if (!bundle.members.empty()) {
            string alias_target = _genStaticMemberTypeStr(bundle.members[0]);
            out_lines.push_back("using " +  bundle.name + " = " + alias_target + ";\n");
        }
    } else if (!bundle.enum_members.empty()) {
        out_lines.push_back("enum class " + bundle.name + " {\n");
        for (const auto &enum_member : bundle.enum_members) {
            if (enum_member.has_value) {
                out_lines.push_back(CodeTab + enum_member.name + " = " + std::to_string(enum_member.value) + ",\n");
            } else {
                out_lines.push_back(CodeTab + enum_member.name + ",\n");
            }
        }
        out_lines.push_back("};\n");
    } else {
        out_lines.push_back("struct " + bundle.name + " {\n");
        for (const auto &member : bundle.members) {
            string type_str = _genStaticMemberTypeStr(member);
            out_lines.push_back(CodeTab + type_str + " " + member.name + ";\n");
        }
        out_lines.push_back("};\n");
    }

    return out_lines;
}

} // namespace detail

using namespace detail;

vector<string> genStaticConfigHeaderCode(const VulStaticConfigLib &configlib) {
    vector<string> out_lines = genHeaderPrelude();

    out_lines.push_back("#include \"common.h\"\n");
    out_lines.push_back("#include \"fixint.hpp\"\n");
    out_lines.push_back("\n");
    out_lines.push_back("// Configuration Items\n");
    out_lines.push_back("\n");

    for (const auto &cfg : configlib) {
        out_lines.push_back("constexpr int64_t " + cfg.first + " = " + std::to_string(cfg.second) + ";\n");
    }

    return out_lines;
}

vector<string> genStaticGlobalHelperHeaderCode(const vector<string> &helper_codes) {
    vector<string> out_lines = genHeaderPrelude();
    out_lines.push_back("#include \"common.h\"\n");
    out_lines.push_back("#include \"fixint.hpp\"\n");
    out_lines.push_back("\n");
    out_lines.push_back("// Global Helper Definitions\n");
    out_lines.push_back("\n");
    for (const auto &line : helper_codes) {
        if (line.ends_with("\n")) {
            out_lines.push_back(line);
        } else {
            out_lines.push_back(line + "\n");
        }
    }
    return out_lines;
}

vector<string> genStaticBundle(const VulStaticBundle &bundle) {
    return _genStaticBundle(bundle);
}

vector<string> genStaticBundleHeaderCode(const VulStaticBundleLib &bundlelib) {
    vector<string> out_lines = genHeaderPrelude();

    out_lines.push_back("#include \"common.h\"\n");
    out_lines.push_back("#include \"config.h\"\n");
    out_lines.push_back("#include \"fixint.hpp\"\n");
    out_lines.push_back("\n");
    out_lines.push_back("// Bundle Definitions\n");
    out_lines.push_back("\n");


    for (const auto &bundle : bundlelib) {
        auto bundle_lines = _genStaticBundle(bundle);
        out_lines.insert(out_lines.end(), bundle_lines.begin(), bundle_lines.end());
        out_lines.push_back("\n");
    }

    return out_lines;
}

vector<string> genStaticProjectHeaderCode(
    const VulStaticConfigLib &configlib,
    const VulStaticBundleLib &bundlelib,
    const vector<string> &helper_codes
) {
    vector<string> out_lines = genHeaderPrelude();

    out_lines.push_back("#include \"common.h\"\n");
    out_lines.push_back("#include \"fixint.hpp\"\n");
    out_lines.push_back("\n");

    out_lines.push_back("// Configuration Items\n");
    out_lines.push_back("\n");
    for (const auto &cfg : configlib) {
        out_lines.push_back("constexpr int64_t " + cfg.first + " = " + std::to_string(cfg.second) + ";\n");
    }
    out_lines.push_back("\n");

    out_lines.push_back("// Bundle Definitions\n");
    out_lines.push_back("\n");
    for (const auto &bundle : bundlelib) {
        auto bundle_lines = _genStaticBundle(bundle);
        out_lines.insert(out_lines.end(), bundle_lines.begin(), bundle_lines.end());
        out_lines.push_back("\n");
    }

    out_lines.push_back("// Global Helper Definitions\n");
    out_lines.push_back("\n");
    for (const auto &line : helper_codes) {
        if (line.ends_with("\n")) {
            out_lines.push_back(line);
        } else {
            out_lines.push_back(line + "\n");
        }
    }

    return out_lines;
}


} // namespace simgen
