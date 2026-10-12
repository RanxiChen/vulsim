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

#include "rtlgen.h"

namespace rtlgen::detail {

struct ArgPort {
    string name;
    VulStaticTypeSignature type;
    uint32_t width;
    vector<FlatField> flat_fields;
};


string uintExtractExpr(const string &var, uint32_t high, uint32_t low);
string typedExtractExpr(const FlatField &field, const string &var);
string packFlatFieldExpr(const FlatField &field, const string &value_expr);
string flatFieldValueExpr(const string &root, const string &flat_name);
string reqservFieldValueExpr(const string &root, const string &decl_name, const string &flat_name);
string reqservArraySuffix(uint32_t idx);
ArgPort procArg(const VulStaticArg &arg, const VulStaticBundleLib &bundlelib);
string reqservValueArgList(const VulStaticReqServ &reqserv);
string reqservVldPort(const string &reqserv_name);
string reqservRdyPort(const string &reqserv_name);
string reqservArgPort(const string &reqserv_name, const string &arg_name);
string queryPort(const string &query_name);

} // namespace rtlgen::detail
