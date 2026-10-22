# 4×4 矩阵乘法脉动阵列回归

计算 `C[i][j] = Σ(k=0..3) A[i][k] * B[k][j]`。A、B 元素为无符号
8 位整数，C 使用 32 位无符号累加器，最大结果为 `4*255*255 = 260100`，
不截断、不溢出。原来的位向量 FPU 占位模块已移除，乘加直接在 PE 中实现。

## 硬件结构与接口

- Top 通过 `CHILD_INSTANCE(PE, mesh, dims=[SIZE,SIZE])` 创建 16 个独立 PE，
  并将两个实例维度分别绑定到 ROW、COL 参数。
- `left`、`up` 是各含 4 路的边界 SERVICE，参数为 `{uint8_t data; bool valid;}`。
  A 从左边界向右传播，B 从上边界向下传播；水平和垂直连接使用 `$`、`?`
  规则展开。末列、末行的输出连接到父模块边界服务。
- 每个 PE 保存 A、B 流水寄存器、局部累加和与有效乘加次数。只有两个操作数
  都有效时才累加。每周期前进一格，无输入事务时自动插入无效空拍。
- `clear()` 批量调用所有 PE 的 clear 服务，清零累加和及乘加次数；调用前应
  排空流水线。`sim_reset()` 同时清除累加器和流水线，可用于中断计算。
- `snapshot()` 通过二维通配 QUERY 返回所有 PE 的 `PEState` 值快照，形状为
  `std::array<std::array<PEState,4>,4>`，每项包含 `sum` 与 `products`。

## 输入时序

设第 k 对数据的基准注入周期为 `launch[k]`。第 i 路左边界在
`launch[k]+i` 注入 `A[i][k]`，第 j 路上边界在 `launch[k]+j` 注入
`B[k][j]`。二者在 PE[i][j] 同时到达，乘加于
`launch[k]+i+j+1` 周期完成并在该周期 `sim_nextcycle()` 后可见。

无空拍时可令 `launch[k]=k`：从 t=0 注入到 t=10 的周期提交后，全部结果完成。
通过拉开 launch 间隔，可以在计算中插入全局空拍；两侧都必须遵守相同的错位计划。
本实现由调用方管理任务边界，读取快照后排空流水线并 clear 即可开始下一次矩阵乘法。

## 功能测试

Main 的同一套测试同时用于 C++ 仿真与 RTL/Verilator 仿真：

- 5 组定向矩阵：零矩阵、左右单位矩阵、全 255 极值。
- 64 组单非零乘积，遍历所有 `(i,j,k)`，检查行列及归约维度连接。
- 256 组固定种子随机矩阵，覆盖全 8 位范围、小数值、稀疏与随机 0/255 分布。
- 随机前导空闲及乘积之间的空拍；交替使用批量 REQUEST 和显式坐标调用。
- 按数学公式及注入计划逐周期核对全部 16 个 PE 的部分和、有效乘加次数；
  最终结果再次对照普通三重循环矩阵乘法。失败打印种子、任务、周期和 PE 坐标。
- 连续任务通过 clear 分离，完成后保持空闲，检查结果不变；16 次计算中途复位，
  检查流水线没有残留。另覆盖单侧 valid、非零无效操作数及省略边界调用。

固定随机种子为 `0x4A17B29`，C++ 与 RTL 应输出完全一致的 PASS 统计。
生成器的参数绑定、结构变化与错误诊断专项测试另见
`regression/generator_array_instances.py`。

运行全部回归：`./regression.sh`。

单独运行本项目：

```sh
build/vulsimgen -m regression/array/Main.utb -f -o build/array-sim
build/array-sim/run.sh
build/vulrtlgen -m regression/array/Main.utb -f -o build/array-rtl
build/array-rtl/run.sh
```
