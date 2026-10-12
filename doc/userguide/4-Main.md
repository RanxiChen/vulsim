# 4. VulCPP Main 模块定义

Main 是仿真入口，通过 `TOP` 和 `PROJECT` 选择顶层模块与工程目录。所有声明使用与硬件模块一致的参数规则，见 [声明 API 参数表](0-declarations.md)。

## 工程绑定与参数

```cpp
#include <defhelper.hpp>
#include <run.hpp>
#include "header.hpp"

TOP(path="./Top.hpp");
PROJECT(path=".");
PARAMETER(name=WIDTH, value=32);
```

路径相对于 Main 文件所在目录解析。`TOP` 和 `PROJECT` 各只能声明一次；两者也可使用位置参数，如 `TOP("./Top.hpp")`。Main 中的 `PARAMETER` 覆盖顶层同名模块参数。

`defhelper.hpp` 和 `run.hpp` 用于编辑期声明，不进入最终生成代码。项目全局类型与配置由项目 header 提供。标准库头文件可用于仿真代码。

## REQUEST 与 SERVICE

Main 的 `REQUEST` 调用顶层同名 `SERVICE`；Main 的 `SERVICE` 接收顶层同名 `REQUEST`。签名、握手属性和事务维度必须匹配。

```cpp
REQUEST(start, ARG(uint32_t) value);
REQUEST(read, handshake=true, RESP(uint32_t) value);

SERVICE(report, ARG(uint32_t) value) {
    printf("report=%u\n", value);
}

SERVICE(result, ready=true, RESP(uint32_t) value) {
    value = 42;
}
```

参数只读方向用 `ARG`，响应只写方向用 `RESP`；业务参数置于全部属性之后。握手服务必须提供无副作用的 `ready` 条件，不能读取尚未写出的 RESP 值。Main 中的 SERVICE 提供实现代码块。

事务阵列统一使用 `dims=[...]`，显式坐标调用和批量 REQUEST 调用规则与模块一致。

## QUERY

Main 中 `QUERY` 必须是无代码块声明，并与顶层同名 QUERY 返回类型一致：

```cpp
QUERY(name=status, type=Status);
```

生成代码将 `status()` 转发到顶层实例的同名查询函数。

## GLOBAL 与 SIMULATION

使用 `GLOBAL() { ... }` 声明跨周期保存的仿真变量，变量采用普通 C++ 声明；使用 `SIMULATION() { ... }` 定义仿真入口，入口返回即结束仿真。

```cpp
GLOBAL() {
    uint32_t cycles = 0;
}

SIMULATION() {
    sim_reset();
    start(7);
    sim_nextcycle();
    ++cycles;
}
```

- `sim_nextcycle()` 推进一个周期，处理事务交互并提交状态更新。
- `sim_reset()` 将寄存器递归置零，再执行用户可选复位块。
- `sim_exit()` 结束仿真。

Main 可使用输出、文件 I/O 等普通仿真操作。基础位置写法可获得 C++ IDE 类型辅助；上述显式命名写法由 vul 解析检查。旧端口与分离服务实现宏已移除。
