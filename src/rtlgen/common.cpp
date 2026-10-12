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

#include "common.hpp"
#include "apiinline/utils.hpp"
#include <cctype>

namespace rtlgen {
namespace detail {
string uintExtractExpr(const string &var, uint32_t high, uint32_t low) {
    return var + ".at<" + std::to_string(high) + ", " + std::to_string(low) + ">()";
}

string typedExtractExpr(const FlatField &field, const string &var) {
    return apiinline::unpackFlatFieldValueExpr(
        field.name,
        uintExtractExpr(var, field.offset + field.width - 1, field.offset),
        field);
}

string packFlatFieldExpr(const FlatField &field, const string &value_expr) {
    return apiinline::packFlatFieldValueExpr(value_expr, field);
}

string flatFieldValueExpr(const string &root, const string &flat_name) {
    if (flat_name == "value") {
        return root;
    }
    if (flat_name == root) {
        return root;
    }
    constexpr const char *value_prefix = "value.";
    if (flat_name.rfind(value_prefix, 0) == 0) {
        return root + "." + flat_name.substr(std::char_traits<char>::length(value_prefix));
    }
    constexpr const char *value_index_prefix = "value[";
    if (flat_name.rfind(value_index_prefix, 0) == 0) {
        return root + flat_name.substr(std::char_traits<char>::length("value"));
    }
    const string prefix = root + ".";
    if (flat_name.rfind(prefix, 0) == 0) {
        return root + "." + flat_name.substr(prefix.size());
    }
    return flat_name;
}

string reqservFieldValueExpr(
    const string &root,
    const string &decl_name,
    const string &flat_name
) {
    if (flat_name == decl_name) {
        return root;
    }
    const string member_prefix = decl_name + ".";
    if (flat_name.rfind(member_prefix, 0) == 0) {
        return root + "." + flat_name.substr(member_prefix.size());
    }
    const string index_prefix = decl_name + "[";
    if (flat_name.rfind(index_prefix, 0) == 0) {
        return root + flat_name.substr(decl_name.size());
    }
    return flatFieldValueExpr(root, flat_name);
}

string reqservArraySuffix(uint32_t idx) {
    return "__IDX" + std::to_string(idx);
}






ArgPort procArg(const VulStaticArg &arg, const VulStaticBundleLib &bundlelib) {
    ArgPort port;
    port.name = arg.name;
    port.width = 0;
    port.type = arg.type;
    flatten_type_signature(arg.type, bundlelib, arg.name, port.width, port.flat_fields);
    return port;
}

string reqservValueArgList(const VulStaticReqServ &reqserv) {
    string out;
    for (const auto &param : reqserv.param_order) {
        if (!out.empty()) out += ", ";
        if (param.kind == VulReqServParamKind::Arg) {
            const auto &arg = reqserv.args.at(param.index);
            out += arg.type.toString() + " " + arg.name;
        } else {
            const auto &ret = reqserv.rets.at(param.index);
            out += ret.type.toString() + " & " + ret.name;
        }
    }
    return out;
}

string reqservVldPort(const string &reqserv_name) {
    return reqserv_name + "__vld__";
}

string reqservRdyPort(const string &reqserv_name) {
    return reqserv_name + "__rdy__";
}

string reqservArgPort(const string &reqserv_name, const string &arg_name) {
    return reqserv_name + "_" + arg_name + "__";
}

string queryPort(const string &query_name) {
    return query_name + "__query__";
}



} // namespace detail

} // namespace rtlgen
