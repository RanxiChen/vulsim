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

namespace rtlgen {
namespace detail {
void _procQueues(RTLGenContext &ctx) {

    auto clog2 = [](uint32_t x) -> uint32_t {
        uint32_t res = 0;
        while ((1U << res) < x) {
            res++;
        }
        return res;
    };

    for (const auto &que : ctx.module.queues) {

        VulErrorContextGuard que_guard("processing queue " + que.name);

        string que_name = que.name;

        bool is_mp = (que.deq_width > 1) || (que.enq_width > 1);

        string port_enqready = que_name + "__enqready__";
        string port_deqready = que_name + "__deqready__";
        string port_enqvalid = que_name + "__enqvalid__";
        string port_deqvalid = que_name + "__deqvalid__";
        string port_enqdata = que_name + "__enqdata__";
        string port_deqdata = que_name + "__deqdata__";
        string port_clrnext = que_name + "__clrnext__";

        uint32_t data_width = 0;
        vector<FlatField> data_fields;
        flatten_type_signature(que.type, ctx.local_bundlelib, "value", data_width, data_fields);

        string proxy_class_name = "QueueProxy_" + que_name + "__";
        string data_width_str = std::to_string(data_width);
        string data_width_m1_str = std::to_string(data_width - 1);
        string depth_str = std::to_string(que.depth);
        string enq_width_str = std::to_string(que.enq_width);
        string deq_width_str = std::to_string(que.deq_width);
        string enq_cnt_width_str = std::to_string(clog2(que.enq_width + 1));
        string deq_cnt_width_str = std::to_string(clog2(que.deq_width + 1));
        string data_type_str = que.type.toString();

        if (que.depth == 0) {
            throw VulException("Queue must have positive depth");
        }
        if (que.enq_width == 0 || que.deq_width == 0) {
            throw VulException("Queue must have positive enq_width and deq_width");
        }

        if (!is_mp) {
            string enqready_type_str = "bool";
            string deqvalid_type_str = "bool";
            string enqvalid_type_str = "bool";
            string deqready_type_str = "bool";
            string enqdata_type_str = "Int<" + data_width_str + ">";
            string deqdata_type_str = "Int<" + data_width_str + ">";



            ctx.hls_arguments.push_back("const " + enqready_type_str + " " + port_enqready);
            ctx.hls_arguments.push_back("const " + deqvalid_type_str + " " + port_deqvalid);
            ctx.hls_arguments.push_back(enqvalid_type_str + " &" + port_enqvalid);
            ctx.hls_arguments.push_back(deqready_type_str + " &" + port_deqready);
            ctx.hls_arguments.push_back(enqdata_type_str + " &" + port_enqdata);
            ctx.hls_arguments.push_back("const " + deqdata_type_str + " " + port_deqdata);
            ctx.hls_arguments.push_back("bool &" + port_clrnext);

            ctx.hls_init.push_back(port_enqvalid + " = false;\n");
            ctx.hls_init.push_back(port_deqready + " = false;\n");
            ctx.hls_init.push_back(port_enqdata + " = 0;\n");
            ctx.hls_init.push_back(port_clrnext + " = false;\n");


            ctx.rtl_decl.push_back("wire " + port_enqready + ";\n");
            ctx.rtl_decl.push_back("wire " + port_deqvalid + ";\n");
            ctx.rtl_decl.push_back("wire " + port_enqvalid + ";\n");
            ctx.rtl_decl.push_back("wire " + port_deqready + ";\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_enqdata + ";\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_deqdata + ";\n");
            ctx.rtl_decl.push_back("wire " + port_clrnext + ";\n");

            ctx.rtl_logicports.push_back("." + port_enqready + "(" + port_enqready + ")");
            ctx.rtl_logicports.push_back("." + port_deqvalid + "(" + port_deqvalid + ")");
            ctx.rtl_logicports.push_back("." + port_enqvalid + "(" + port_enqvalid + ")");
            ctx.rtl_logicports.push_back("." + port_deqready + "(" + port_deqready + ")");
            ctx.rtl_logicports.push_back("." + port_enqdata + "(" + port_enqdata + ")");
            ctx.rtl_logicports.push_back("." + port_deqdata + "(" + port_deqdata + ")");
            ctx.rtl_logicports.push_back("." + port_clrnext + "(" + port_clrnext + ")");

            ctx.rtl_inst.push_back("VulQueue #(\n");
            ctx.rtl_inst.push_back("  .Width(" + data_width_str + "),\n");
            ctx.rtl_inst.push_back("  .Depth(" + depth_str + ")\n");
            ctx.rtl_inst.push_back(") " + que_name + "__inst (\n");
            ctx.rtl_inst.push_back("  .clk(clk),\n");
            ctx.rtl_inst.push_back("  .rstn(rstn),\n");
            ctx.rtl_inst.push_back("  .enqready(" + port_enqready + "),\n");
            ctx.rtl_inst.push_back("  .deqready(" + port_deqvalid + "),\n");
            ctx.rtl_inst.push_back("  .enqnext_vld(" + port_enqvalid + "),\n");
            ctx.rtl_inst.push_back("  .enqnext_data(" + port_enqdata + "),\n");
            ctx.rtl_inst.push_back("  .deqnext_vld(" + port_deqready + "),\n");
            ctx.rtl_inst.push_back("  .deqnext_data(" + port_deqdata + "),\n");
            ctx.rtl_inst.push_back("  .clrnext(" + port_clrnext + ")\n");
            ctx.rtl_inst.push_back(");\n");
        } else {
            // Match the queue RTL's native count ports. API helpers widen the
            // values to uint32_t without creating a mismatched logic-module ABI.
            string enqready_type_str = "Int<" + enq_cnt_width_str + ">";
            string deqvalid_type_str = "Int<" + deq_cnt_width_str + ">";
            string enqvalid_type_str = "Int<" + enq_cnt_width_str + ">";
            string deqready_type_str = "Int<" + deq_cnt_width_str + ">";
            string enqdata_type_str = "std::array<Int<" + data_width_str + ">, " + enq_width_str + ">";
            string deqdata_type_str = "std::array<Int<" + data_width_str + ">, " + deq_width_str + ">";



            ctx.hls_arguments.push_back("const " + enqready_type_str + " " + port_enqready);
            ctx.hls_arguments.push_back("const " + deqvalid_type_str + " " + port_deqvalid);
            ctx.hls_arguments.push_back(enqvalid_type_str + " &" + port_enqvalid);
            ctx.hls_arguments.push_back(deqready_type_str + " &" + port_deqready);
            ctx.hls_arguments.push_back(enqdata_type_str + " &" + port_enqdata);
            ctx.hls_arguments.push_back("const " + deqdata_type_str + " &" + port_deqdata);
            ctx.hls_arguments.push_back("bool &" + port_clrnext);

            ctx.hls_init.push_back(port_enqvalid + " = 0;\n");
            ctx.hls_init.push_back(port_deqready + " = 0;\n");
            for (uint32_t i = 0; i < static_cast<uint32_t>(que.enq_width); ++i) {
                ctx.hls_init.push_back(port_enqdata + "[" + std::to_string(i) + "] = 0;\n");
            }
            ctx.hls_init.push_back(port_clrnext + " = false;\n");


            ctx.rtl_decl.push_back("wire [" + enq_cnt_width_str + "-1:0] " + port_enqready + ";\n");
            ctx.rtl_decl.push_back("wire [" + deq_cnt_width_str + "-1:0] " + port_deqvalid + ";\n");
            ctx.rtl_decl.push_back("wire [" + enq_cnt_width_str + "-1:0] " + port_enqvalid + ";\n");
            ctx.rtl_decl.push_back("wire [" + deq_cnt_width_str + "-1:0] " + port_deqready + ";\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_enqdata + "[" + enq_width_str + "];\n");
            ctx.rtl_decl.push_back("wire [" + data_width_m1_str + ":0] " + port_deqdata + "[" + deq_width_str + "];\n");
            ctx.rtl_decl.push_back("wire " + port_clrnext + ";\n");

            ctx.rtl_logicports.push_back("." + port_enqready + "(" + port_enqready + ")");
            ctx.rtl_logicports.push_back("." + port_deqvalid + "(" + port_deqvalid + ")");
            ctx.rtl_logicports.push_back("." + port_enqvalid + "(" + port_enqvalid + ")");
            ctx.rtl_logicports.push_back("." + port_deqready + "(" + port_deqready + ")");
            ctx.rtl_logicports.push_back("." + port_enqdata + "(" + port_enqdata + ")");
            ctx.rtl_logicports.push_back("." + port_deqdata + "(" + port_deqdata + ")");
            ctx.rtl_logicports.push_back("." + port_clrnext + "(" + port_clrnext + ")");

            ctx.rtl_inst.push_back("VulQueueMP #(\n");
            ctx.rtl_inst.push_back("  .Width(" + data_width_str + "),\n");
            ctx.rtl_inst.push_back("  .Depth(" + depth_str + "),\n");
            ctx.rtl_inst.push_back("  .EnqWidth(" + enq_width_str + "),\n");
            ctx.rtl_inst.push_back("  .DeqWidth(" + deq_width_str + ")\n");
            ctx.rtl_inst.push_back(") " + que_name + "__inst (\n");
            ctx.rtl_inst.push_back("  .clk(clk),\n");
            ctx.rtl_inst.push_back("  .rstn(rstn),\n");
            ctx.rtl_inst.push_back("  .enqready(" + port_enqready + "),\n");
            ctx.rtl_inst.push_back("  .deqready(" + port_deqvalid + "),\n");
            ctx.rtl_inst.push_back("  .enqnext_vld(" + port_enqvalid + "),\n");
            ctx.rtl_inst.push_back("  .enqnext_data(" + port_enqdata + "),\n");
            ctx.rtl_inst.push_back("  .deqnext_vld(" + port_deqready + "),\n");
            ctx.rtl_inst.push_back("  .deqnext_data(" + port_deqdata + "),\n");
            ctx.rtl_inst.push_back("  .clrnext(" + port_clrnext + ")\n");
            ctx.rtl_inst.push_back(");\n");
        }
    }
}


} // namespace detail

} // namespace rtlgen
