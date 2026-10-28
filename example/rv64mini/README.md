# RV64Mini：R/I 整数计算与单笔 Mem

源码：Alan /home/chen/CISLC-WORK/vulsim/example/rv64mini。
框架基线：60fbd75c5018c4accd0de2e3e9a49b89fd02fffb，工作区原有修改未动。
本次改动限于本目录和父工作区的 rv64mini-architecture.md，没有修改 VUL/RTLzz 框架。
此前 ALU 版本的源文件备份在 /tmp/rv64mini-before-mem-1793091798186242538。

## 数据通路

4 项指令 FIFO -> ID/Issue -> ALU 或 LSU -> WB -> RF。

ID 同拍译码、生成立即数、读 RF、检查当前 RAW/WAW，成功发射时锁存操作数。
源 busy 或目的 busy 都阻止发射；不做 WB 到 ID 的同周期旁路。
R 型使用两个源，I 型计算/Load 不读取或检查伪 rs2，Store 使用两个源。
ALU 输入和结果分别有寄存器，计算结果最早经过结果寄存器后进入 WB。

- Decoder.vul：R/I ALU 与 Load/Store 译码。
- ImmGen.vul：I/S 型立即数生成。
- ALU.vul：组合整数运算。
- LSU.vul：组合地址计算、对齐检查和 Load 扩展。
- TopModule.vul：流水寄存器、scoreboard、批次计数、WB 8+1 表和 Mem 状态机。
- RegisterFile.vul：原正常/FASE 接口保持不变。

支持 ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND；
ADDI/SLTI/SLTIU/XORI/ORI/ANDI/SLLI/SRLI/SRAI；
LB/LH/LW/LD/LBU/LHU/LWU 与 SB/SH/SW/SD。
尚无 OP-32/OP-IMM-32、Mul/Div/FP/Branch、Cache、MMU 或完整特权/异常返回。
不支持编码置 unsupported 并停止后续发射，已有工作可排空。

## 批次与 WB

每个寄存器有 busy 和 producer_batch；每个批次有 in_flight。
批次 ID 为 1024 项环形空间。Mem 是本批最后一条；成功发射 Mem 后 issue_batch 加一。
批内计算可乱序提交，只有当前 commit_batch 可以实际写 RF。
边界等本批其余指令全部提交后再处理，自身完成后才推进 commit_batch。
同批发射和提交的计数更新合并，不能依赖两个写端口覆盖来合并。

ALU 结果先尝试直接提交；不能提交才进 8 项等待表。
表满后启用单项 overflow，overflow 有效时停止接收新 WB 结果、暂停年轻流水线推进。
等待表使用轮询游标。没有在 ID 预留表项，未被 WB 接收的结果留在 FU 流水寄存器中。
更老 Mem 的响应与最终提交不受年轻 overflow 阻断，否则会形成边界无法解除的死锁。

顶层 result(Writeback) 的 ready 是当前测试用的提交接受信号。
只有该握手成功，才写 RF、清 busy、减批次 in_flight。
Writeback 携带 inst、pc、batch、rd、writes_rd、is_mem、data。
本次保留的 PC 是输入指令流从 0 开始、每次接受加 4 的标记，没有取指/分支重定向。
FASE 仍只在 RF 模块中，尚未接入核心控制。

## Mem 接口与时序

最多一笔 Mem，Load/Store 共用；只有所属批次已轮到提交时才允许 Mem 发射。
后续计算可以执行并等待提交，但后续 Mem 不提前发射。
单笔 Mem 在响应后仍持有边界状态，直到最终提交/异常处理才释放。

1. ID 发射时锁存 base、offset、Store 数据和元数据。
2. 下一拍计算地址，检查自然对齐。
3. mem_req 请求握手；未接受时保持全部参数，不重复发送已接受请求。
4. mem_resp 响应握手，Load 扩展数据，Store 等待完成确认。
5. 结果保持；本批计数为 1 时才最终提交或处理异常。

mem_req 是顶层带握手 REQUEST，TestMain 用同名 SERVICE 模拟下一级。
MemRequest 为字节地址 address、低位对齐 wdata、bytes=1/2/4/8、write、batch。
mem_resp 是顶层带握手 REQUEST，TestMain 的 SERVICE 在响应有效时返回 rdata/error。
rdata 的最低位对应请求起始地址；没有 AXI 地址低位移位或 burst 打包约定。
这是简化 ready-valid 请求/响应接口，不是 AXI 总线实现，没有多个 outstanding/burst。

非对齐在发请求前检出，原因码 Load/Store=4/6。
下一级 error 对应 Load/Store access-fault=5/7。
失败 Load 不写 RF。等本批前序全部提交后，trap 报告 PC/地址/原因/批次，
按 producer_batch 清除年轻 busy，并清空年轻流水、WB 缓冲和 FIFO。
当前进入 trapped 停止状态，直到复位；没有 trap vector/CSR/恢复执行路径。
Store 下一级若部分写入后报错，其外部副作用由下一级契约决定，本核心没有内存写回滚。
本轮未实现 Branch，但父目录架构文档保存了其预测与分批恢复约定。

## 测试

Main.utb 模拟延迟内存和请求背压，并检查提交 PC/结果/批次、无重复请求、单笔在途、
存储宽度与符号扩展、负偏移、RAW/WAW、x0、表满/overflow 背压、老 Mem 解除阻塞、
1024 批次 ID 回绕，以及 Load/Store 对齐/下一级错误的四组异常清理。

C++ 仿真结果：

- 正常用例：1100 条指令、1046 个请求、8312 周期。
- Load/Store 非对齐及访问错误四组：各保留 3 条老计算提交，年轻结果全部取消。
- 五组共报告 66121 项检查；覆盖 8 项表满、溢出、请求保持和 ID 回绕。
- 目录：/tmp/rv64mini-mem-20261027-v3-sim。

RegFileMain.utb 保持 138 周期、552 项断言；C++ 和 RTL/Verilator 均通过。
目录为 /tmp/rv64mini-mem-rf-20261027-sim 和 /tmp/rv64mini-mem-rf-20261027-rtl。

主核心默认 RTL 生成成功，Verilator 同一测试通过，结果与上述 C++ 一致。
目录：/tmp/rv64mini-mem-20261027-v3-rtl；未关闭 Verilator 警告。

没有运行综合、布局布线、形式验证或实板测试。以上都是对应源码快照的功能与周期行为证据。

重新生成时，在 Alan 加载环境、使用新的输出目录（不覆盖已有证据）：

    source /home/chen/CISLC-WORK/env.sh
    cd /home/chen/CISLC-WORK/vulsim/build
    ./vulsimgen -m ../example/rv64mini/Main.utb -o <新的C++输出目录>
    bash <新的C++输出目录>/run.sh
    ./vulrtlgen -m ../example/rv64mini/Main.utb -o <新的RTL输出目录>
    bash <新的RTL输出目录>/run.sh
