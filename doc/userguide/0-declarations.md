# VULC++ 声明 API 参数表

声明宏使用唯一的全大写名称，属性使用唯一的小写键。下表给出固定顺序；带默认值的参数可省略，其余必填。所有参数都可以显式命名。

位置参数只能构成固定顺序的前缀；第一次命名后只能继续命名。跳过中间参数需要命名后续参数，不能使用空参数占位。命名参数之间无顺序要求。重复赋值（包括位置与命名重复）、未知键、空值、超出位置数量均报错。

```cpp
REGISTER(counter, uint32_t, 2);
REGISTER(counter, uint32_t, ports=2);
REGISTER(name=counter, ports=2, type=uint32_t);
```

## 固定顺序与默认值

| 宏 | 参数顺序 |
|---|---|
| `CONFIG`、`PARAMETER` | `name, value` |
| `STRUCT`、`ENUM` | `name` |
| `ALIAS` | `name, type, dims=[]` |
| `REGISTER` | `name, type, ports=1, dims=[]` |
| `WIRE` | `name, type` |
| `REQUEST` | `name, handshake=false, dims=[]`，末尾业务参数序列 |
| `SERVICE` | `name, handshake=false, dims=[], priority=0, ready`，末尾业务参数序列；ready 可省略 |
| `QUERY` | `name, type` |
| `CHILD_INSTANCE` | `module, name, dims=[]`，末尾参数覆盖和坐标绑定 |
| `USE_CHILD_SERVICE` | `instance, service, alias, dims`，末尾业务参数序列；dims 可省略以推断通配形状 |
| `USE_CHILD_QUERY` | `instance, query, alias, type, dims`；dims 可省略以推断通配形状 |
| `BRAM` | `name, type, size, read_ports=1, write_ports=1, mode=generic` |
| `ROM` | `name, data_width, size, read_ports=1, init` |
| `QUEUE` | `name, type, depth, enq_width=1, deq_width=1` |
| `CONNECT_CR_CS` | `srcchild, srcreq, dstchild, dstserv` |
| `CONNECT_CR_S` | `srcchild, srcreq, dstserv` |
| `CONNECT_CR_R` | `srcchild, srcreq, dstreq` |
| `CONNECT_S_CS` | `srcserv, dstchild, dstserv` |
| `VERSION`、`USE_VERSION` | `name` |
| `TOP`、`PROJECT` | `path` |

`HELPER()`、`INTERFACE()`、`TICK_IMPL()`、`GLOBAL()`、`SIMULATION()` 无参数。各宏的允许作用域与原有规则一致；测试入口与模块使用相同的声明参数规则。

## 维度、事务和子实例

- 维度必须写成 `dims=[N0,N1,...]`。显式空列表 `[]` 表示标量；数组维度必须是正整数常量表达式。省略普通声明的 dims 表示标量；省略子接口引出的 dims 则按通配实例推断。
- 事务业务参数使用 `ARG(T) x`、`RESP(T) y`，位于所有属性之后，按源码顺序形成调用签名。可以不提供任何业务参数。
- `handshake` 接受 `true/false` 或 `1/0`。SERVICE 的 `ready=条件` 隐含握手；与显式 `handshake=false` 冲突。握手 SERVICE 实现必须有 ready 条件，前置声明不能有 ready。
- 子模块参数覆盖只接受 `PARAM(name)=value`；坐标绑定只接受 `COORD(dim)=parameter`。二者是 CHILD_INSTANCE 的命名尾参数，可交错出现，不能重复。
- BRAM 的 mode 只接受 `generic`、`1rw`；1rw 模式要求 read_ports 和 write_ports 求值后均为 1。

```cpp
REQUEST(fetch, handshake=true, dims=[2,3], ARG(uint32_t) address, RESP(uint32_t) data);
SERVICE(fetch, ready=true, dims=[2,3], ARG(uint32_t) address, RESP(uint32_t) data) {
    data = address;
}
CHILD_INSTANCE(Node, lane, dims=[4], PARAM(WIDTH)=32, COORD(0)=ROW);
BRAM(memory, uint32_t, 1024, mode=1rw);
```

运行期资源方法、事务调用坐标、连接端点索引和通配规则保持原有语义，详见对应章节。

## 迁移规则

| 已移除写法 | 替代声明 |
|---|---|
| 数组类型别名变体 | `ALIAS(..., dims=[...])` |
| 多写端口／数组寄存器变体 | `REGISTER(..., ports=N, dims=[...])` |
| 握手 REQUEST 变体 | `REQUEST(..., handshake=true)` |
| 握手／优先级 SERVICE 变体 | `SERVICE(..., ready=条件, priority=P)` |
| 子实例数组变体 | `CHILD_INSTANCE(..., dims=[...])` |
| 子服务 PORT 引出变体 | `USE_CHILD_SERVICE(...)` |
| 单口 BRAM 变体 | `BRAM(..., mode=1rw)` |
| 多端口 QUEUE 变体 | `QUEUE(..., enq_width=E, deq_width=D)` |
| `array=N`、`ARRAY(N)`、`dim=N`、裸维度追加 | `dims=[N]` |
| `portnum`、`readports`、`writeports` | `ports`、`read_ports`、`write_ports` |
| `enqwidth`、`deqwidth`、`init_path` | `enq_width`、`deq_width`、`init` |
| 子实例裸参数 `WIDTH=32` | `PARAM(WIDTH)=32` |

连接继续使用四个 `CONNECT_*` 宏，每个宏支持上述命名规则。旧端口声明和拆分 SERVICE 条件／逻辑的宏已移除，使用 REQUEST 或完整 SERVICE 声明／实现。

## C++ IDE 辅助支持

`defhelper.hpp` 只服务于编辑期，最终生成工具不使用其中的宏展开。基础支持范围如下：

- 位置式配置、参数、结构体、枚举、标量别名、WIRE 和 QUERY。
- 标量 REGISTER，可带位置式写端口数和可选复位块。
- REQUEST 的标量业务参数；握手值可省略或使用位置式 `true/false/1/0`，握手调用返回 bool，普通调用返回 void。
- SERVICE 的基础业务签名和逻辑块；不支持带 ready 等属性的完整编辑期类型检查。
- 位置式 BRAM、QUEUE、ROM 资源对象，其中 ROM 需提供读端口数和字符串路径。
- 基础子实例、带业务签名的子服务引出、QUERY 引出、四种连接及仿真入口。

例如下面写法可由普通 C++20 编译器进行类型检查：

```cpp
REGISTER(counter, uint32_t, 2);
REQUEST(read, true, RESP(uint32_t) value);
QUEUE(q, uint32_t, 8, 2, 3);
BRAM(memory, uint32_t, 1024, 1, 1, 1rw);
ROM(lookup, 32, 16, 1, "lookup.hex");
```

标准 C++ 预处理器不能从任意 `name=identifier` 中提取声明标识符；方括号也不能保护多维列表中的逗号。因此任意显式命名声明与完整多维方括号声明由 vul 检查，本次不提供源码转换或 IDE 插件。子服务引出在辅助头中使用 bool 返回占位，准确握手属性以子模块声明和 vul 检查结果为准。
