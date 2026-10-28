# FASE 注入握手第一层（2026-10-28）

`fase_inject` 是显式信号 SERVICE（handshake=0）：输入 inst64、inst64_raw[31:0]、inst64_nowait；输出 inst64_ready、inst64_busy。

发送方保持请求直到 ready，应答下一周期撤销或换成新请求。
IDLE 捕获指令及 nowait；PENDING 等待 FIFO，busy=1；成功入队后进入 ACK，ready=1、busy=0，保持一拍再回 IDLE。ACK 不捕获请求，防止同一请求重复入队。ACK 后继续保持请求会被视为下一请求。

完成仅指入现有 4 项指令 FIFO，不代表执行或写回。nowait 两种取值暂以入队为完成；stop_fetch、flush、核心接管与执行完成反馈后续实现。
普通 push_inst 保留；PENDING 时 FASE 独占入队。IDLE 同周期普通入队和 FASE 请求捕获可同时发生，普通指令先入 FIFO。PC 沿用原入队递增标记。
核心已有 trap 清理取消尚未入队的 FASE 请求，不产生成功应答。unsupported/trapped 状态不接受新请求；已停止的核心恢复需复位。

FaseMain.utb 覆盖空队列、满队列保持、两种 nowait、普通/FASE 竞争、连续请求和应答防重复，并校验输出顺序和 PC。
C++ 仿真：/tmp/rv64mini-fase-20261028-v4-sim，48 周期、124 检查、8 条指令通过。
Main.utb C++ 回归：/tmp/rv64mini-fase-regression-20261028-sim，66121 检查通过。
修改前快照：/tmp/rv64mini-before-fase-20261028。
RTL 生成及 Verilator 同组测试通过：/tmp/rv64mini-fase-20261028-v4-rtl，48 周期、124 检查、8 条指令，与 C++ 一致；未关闭 Verilator 警告。未运行综合、布局布线或实板。
