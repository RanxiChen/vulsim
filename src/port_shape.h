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
#include "module.h"

string portArrayType(string type, const vector<ConfigRealValue> &dims);
vector<ConfigRealValue> portCoordinates(uint32_t index, const vector<ConfigRealValue> &dims);
string portSubscript(uint32_t index, const vector<ConfigRealValue> &dims);
string portIndexExpression(const string &coordinates, const vector<ConfigRealValue> &dims);
string portFlatName(const string &name);
string portBulkArguments(const VulStaticReqServ &port);
string portBulkBody(const VulStaticReqServ &port, const string &callee, bool lambda = false);
string portRequestFacade(const VulStaticReqServ &port);
vector<uint32_t> portCallSlots(const vector<ConfigRealValue> &dims, const string &coordinates, const VulStaticConfigLib &config);
string lowerPortIndexDims(string code, const vector<ConfigRealValue> &dims, const VulStaticConfigLib &config);
vector<string> lowerPortIndexDims(const vector<string> &code, const vector<ConfigRealValue> &dims, const VulStaticConfigLib &config);
