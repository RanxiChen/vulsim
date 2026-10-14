# 2. VulCPP header 常量与数据结构定义

在 VulCPP 项目中，全局常量和数据结构被定义在项目级 header 中。工具会在候选项目根目录中查找下面二者之一：

- `header/` 目录：推荐的新形式，目录内所有 `.h` / `.hpp` 文件共同组成项目级 header。
- `header.hpp` 或 `header.h`：兼容旧项目或小型项目的单文件形式。

两种形式不能同时存在。项目级 header 中定义的全局常量、结构体、枚举和类型别名会在所有模块中通用，即使某个模块没有显式 `#include` 对应 header 文件。

当使用 `header/` 目录时，目录内 `.h` / `.hpp` 文件会通过 C++ 风格的 `#include` 关系建立解析顺序：被 include 的文件先解析，include 它的文件后解析；循环 include 会报错。每个文件都按旧的 `header.hpp` 规则解析。

全局 `CONFIG` 和全局 bundle（`STRUCT`、`ENUM`、`ALIAS`）共享同一个项目级命名域，不允许重名或覆盖；例如 `CONFIG(Word, 32)` 与 `STRUCT(Word)` 会报错。

项目根目录查找顺序： `-p/--project` → Main 中的 `PROJECT`（如有）→ Top 模块文件中的 `PROJECT` → Top 文件所在目录的优先级查找项目 header。命令行相对路径基于当前工作目录，`PROJECT` 相对路径基于声明所在文件的目录。全部未找到时输出警告并跳过 header 解析。

项目级 header 文件的内容主要由一系列宏定义构成，用于声明全局常量、结构体、类型别名等。
下面列举了所有能用在项目级 header 中的宏定义

## 补充：VulCPP 常量值表达式

配置参数值是一个字符串表达式，包含常量、其他配置参数名、和运算符号。

所有合法的参数表达式运算符如下（按照算符优先级从高到底排序）：

| 优先级 | 算符 | 结合方向 | 说明 |
|----|----|----|----|
| 0 | () | 无 | 小括号，用于覆盖其他算符优先级 |
| 1 | @ / clog2(...) | 右 | log2ceil，如@8=3，clog2(15)=4 |
| 1 | - | 右 | 负号 |
| 1 | ~ | 右 | 按位取反 |
| 1 | ! | 右 | 逻辑取反 |
| 2 | * | 左 | 整数乘法 |
| 2 | / | 左 | 整数除法 |
| 2 | % | 左 | 整数取余 |
| 3 | + | 左 | 整数加法 |
| 3 | - | 左 | 整数减法 |
| 4 | << | 左 | 整数逻辑左移 |
| 4 | >> | 左 | 整数算术右移 |
| 5 | < | 左 | 小于 |
| 5 | <= | 左 | 小于等于 |
| 5 | > | 左 | 大于 |
| 5 | >= | 左 | 大于等于 |
| 6 | == | 左 | 等于 |
| 6 | != | 左 | 不等于 |
| 7 | & | 左 | 按位与 |
| 8 | ^ | 左 | 按位异或 |
| 9 | \| | 左 | 按位或 |
| 10 | && | 左 | 逻辑与 |
| 11 | \|\| | 左 | 逻辑或 |
| 12 | ?: | 右 | 三元条件运算符 |

其中 `clog2(expr)` 与 `@expr` 完全等价，只是函数写法在复杂表达式中通常更易读。

如果参数表达式中引用了其他参数名，则会自动解析引用并计算最终值。例如：

```
"PARAM_A" = "8"
"PARAM_B" = "@PARAM_A * 2 + 4"      // PARAM_B的值为log2ceil(8)*2+4=3*2+4=10
"PARAM_C" = "clog2(PARAM_B + 5)"    // PARAM_C的值为log2ceil(15)=4
```

循环引用是非法的。例如：

```
"PARAM_A" = "PARAM_B + 1"
"PARAM_B" = "PARAM_A + 1"
```

## CONFIG(name, value)

定义一个全局常量：
- `name`：常量的名称
- `value`：常量的值表达式

等价于：
```cpp
constexpr int64_t name = value;
```

在编辑期辅助头 `defhelper.hpp` 中也提供了同名 `inline constexpr int64_t clog2(int64_t)`，因此在 C++ 补全与语法检查环境下可以直接书写 `clog2(...)`。

## STRUCT(name)

定义一个全局结构体：
- `name`：结构体的名称

定义方式与标准 C++ 结构体相同，可以包含成员变量。例如：

```cpp
STRUCT(MyStruct) {
    int32_t member1;
    uint64_t member2;
};
```

成员变量类型可以是以下类型之一：
- 标准整数类型：`int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`
- 其他已定义的结构体类型或别名类型
- bool 类型

## ENUM(name)

定义一个全局枚举类型：
- `name`：枚举类型名称

定义方式与 C++ 的 `enum class` 风格一致，例如：

```cpp
ENUM(MyState) {
    Idle = 1,
    Running,
    Waiting = 7,
    Halted
};
```

说明：
- 支持 `成员名 = 常量表达式` 形式显式指定枚举值
- 未显式指定值的成员按 C++ 风格从前一个成员值继续递增
- 常量表达式支持与 `CONFIG` 相同的常量表达式语法
- 枚举类型可以作为 `STRUCT` 成员类型、事务参数类型、寄存器类型、`QUERY` 返回类型等使用

## ALIAS(name, type, dims=[])

定义标量或数组类型别名：

```cpp
ALIAS(Word, uint32_t);
ALIAS(U32x4, uint32_t, dims=[4]);
ALIAS(Matrix, uint8_t, dims=[2, 3]);
ALIAS(name=Matrix2, type=uint8_t, dims=[2, 3]);
```

`dims=[]` 或省略维度表示普通别名；多维数组按维度列表嵌套 `std::array`。

