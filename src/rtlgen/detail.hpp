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
#include "apiinline/register_usage_check.hpp"

namespace rtlgen::detail {

struct RTLGenContext {
    const VulStaticModuleInstance &module;
    const VulStaticConfigLib &local_configlib;
    const VulStaticBundleLib &local_bundlelib;
    const vector<string> &global_helper_codes;
    apiinline::RegisterAPIUsageMap register_api_usage;

    RTLGenContext(
        const VulStaticModuleInstance &module,
        const VulStaticConfigLib &local_configlib,
        const VulStaticBundleLib &local_bundlelib,
        const vector<string> &global_helper_codes
    )
        : module(module),
          local_configlib(local_configlib),
          local_bundlelib(local_bundlelib),
          global_helper_codes(global_helper_codes) {}

    vector<string> hls_header; // 文件开头的常量和结构体声明
    VulDebugLocs hls_header_debug;
    vector<string> hls_arguments; // HLS主函数的参数列表，仅包含类型和参数名字符串，没有逗号或换行
    vector<string> hls_init; // HLS主函数开头的初始化代码，主要是寄存器和线网的定义和reset赋值
    VulDebugLocs hls_init_debug;
    vector<string> hls_reset_init; // 暂存寄存器 resetvalue 计算代码，在 API inline 后插入
    VulDebugLocs hls_reset_init_debug;
    vector<string> hls_helpers; // HLS主函数中定义的辅助函数，用来将函数调用转换成对信号的操作
    VulDebugLocs hls_helpers_debug;
    vector<string> hls_blocks; // HLS主函数中每个逻辑块的代码
    VulDebugLocs hls_blocks_debug;
    vector<string> hls_body; // HLS主函数的主体代码，调用逻辑块
    VulDebugLocs hls_body_debug;
    vector<string> hls_final; // HLS主函数结尾的代码，主要是对输出信号的收尾赋值
    VulDebugLocs hls_final_debug;

    vector<string> rtl_ports;  // RTL框架模块的端口声明，每个一行，没有逗号或换行
    vector<string> rtl_decl;   // RTL框架模块的声明部分代码，主要是寄存器和线网的定义
    vector<string> rtl_logic;  // RTL框架模块的逻辑部分代码，主要是时序逻辑和组合逻辑的实现
    vector<string> rtl_inst;  // RTL框架模块的实例化代码，主要是子模块的实例化和连接
    vector<string> rtl_logicports;  // RTL框架中实例化逻辑子模块时连接的端口代码，每个一行，没有逗号或换行

    vector<string> resource_files;
};

void _procConstAndBundle(RTLGenContext &ctx);
void _procWires(RTLGenContext &ctx);
void _procRegisters(RTLGenContext &ctx);
void _procRequests(RTLGenContext &ctx);
void _procServicesAndTicks(RTLGenContext &ctx);
void _procQueries(RTLGenContext &ctx);
void _procChildrenAndConnection(RTLGenContext &ctx);
void _procQueues(RTLGenContext &ctx);
void _procBRAMAndROM(RTLGenContext &ctx);

} // namespace rtlgen::detail
