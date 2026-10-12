# src 代码索引

核心流程：`vulcpp/` 解析工程 → `module/` 静态化与调度 → `simgen/` 或 `rtlgen/` 生成代码。`cppparse/` 提供源码扫描；`instance_graph` 与 `port_shape` 由分析、验证和生成器共享。根目录头文件保留公共接口，子目录中的 `detail.hpp` 仅供内部实现使用。

## src/apiinline/apiinline.cpp

**文件功能**：串联各类 API inline pass，将模块逻辑代码中的 VUL API 调用替换为端口读写代码。

**主要函数**
- `inlineAPIs(...)`：依次执行 Register、Request、Queue、Memory 等 API inline，生成文件级 helper 函数并仅在用户逻辑中替换 API 调用点，同时保留调试行号映射。

## src/apiinline/apiinline.hpp

**文件功能**：声明 API inline 总入口接口。

**主要函数/类型**
- `inlineAPIs(...)`：对逻辑 HLS 代码执行所有已支持 API 的 inline 替换。

## src/apiinline/bram.cpp

**文件功能**：实现 BRAM/ROM API 调用到 HLS 端口读写的 inline 替换。

**主要函数**
- `inlineMemoryAPIs(...)`：扫描逻辑代码中的 BRAM/ROM 对象调用并替换为端口操作。
- `unpackHelper(...)`：生成将 packed `Int<N>` 还原为内存数据类型的文件级 helper 函数。
- `memoryPortHelpers(...)`：生成 BRAM/ROM 请求、读数据和写数据的文件级函数模板，直接访问全局端口，并将端口号保留为编译期非类型模板参数。
- `emitPack(...)`：生成将内存写入值打包为 `Int<N>` 的代码。
- `bram1rwReqBlock(...)`：生成 1RW BRAM 请求端口赋值代码块。
- `bramWriteBlock(...)`：生成多端口 BRAM 写请求代码块。
- `readReqBlock(...)`：生成 BRAM/ROM 读请求代码块。
- `readDataExpr(...)`：生成 BRAM/ROM 读返回值表达式。

## src/apiinline/bram.hpp

**文件功能**：声明 BRAM/ROM API inline 接口。

**主要函数**
- `inlineMemoryAPIs(...)`：对内存相关 API 调用执行端口化替换。

## src/apiinline/queue.cpp

**文件功能**：实现 Queue/QueueMP API 调用到 HLS 端口读写的 inline 替换。

**主要函数**
- `inlineQueueAPIs(...)`：扫描并替换队列 API 调用。
- `unpackValueHelper(...)`：生成队列出队 packed 数据到原类型的文件级解包 helper。
- `emitPackValue(...)`：生成队列入队值到 packed 数据的打包代码。
- `enqNextBlock(...)`：生成 `enqnext` 对入队端口的赋值代码。
- `frontExpr(...)`：生成 `front` 读取表达式。
- `frontAssignBlock(...)`：将多出队 `front()` 的整数组赋值改写为逐元素赋值。
- `frontHelper(...)`：生成队列 `front` 返回值的文件级 helper。
- `queuePortHelpers(...)`：生成 ready/valid、enq/deq 和 clear 的文件级 helper，直接访问新 ABI 的全局端口变量；多端口计数按 RTL 原生位宽读取后扩展为 API 的 `uint32_t`。
- `deqNextBlock(...)`：生成 `deqnext` 对出队 ready 端口的赋值代码。

## src/apiinline/queue.hpp

**文件功能**：声明 Queue API inline 接口。

**主要函数**
- `inlineQueueAPIs(...)`：对队列相关 API 调用执行端口化替换。

## src/apiinline/register.cpp

**文件功能**：实现 Register API 调用到 HLS 端口读写的 inline 替换。

**主要函数**
- `inlineRegisterAPIs(...)`：扫描并替换寄存器读写 API。
- `readRegisterHelper(...)`：生成从寄存器 packed 读端口还原原类型的文件级 helper。
- `registerWriteHelper(...)`：生成寄存器 `setnext` 文件级函数模板，保持 `value` 等 API 语义参数名、直接访问全局写端口，并保留编译期写端口模板参数。
- `registerControlHelpers(...)`：生成寄存器 `holdnext`/`resetnext` 文件级 helper。
- `writeRegisterBlock(...)`：生成 `setnext` 写使能和写数据端口赋值代码。
- `controlRegisterBlock(...)`：生成 `holdnext`/`resetnext` 控制端口赋值代码。
- `inlineRegisterReadsInExpr(...)`：在表达式内部替换寄存器读操作。

## src/apiinline/register.hpp

**文件功能**：声明 Register API inline 接口。

**主要函数**
- `inlineRegisterAPIs(...)`：对寄存器相关 API 调用执行端口化替换。

## src/apiinline/request.cpp

**文件功能**：实现 Request/Service API 调用到握手端口和参数端口的 inline 替换。

**主要函数**
- `inlineRequestAPIs(...)`：扫描并替换 request/service 调用。
- `emitRequestBody(...)`：生成一次请求调用的 valid、参数和返回值端口操作。
- `requestHelperDef(...)`：为握手与非握手请求统一生成文件级 helper，直接访问全局请求端口；arrayed Request 的槽位保持为编译期非类型模板参数。
- `requestReadyPrelude(...)`：生成直接作为 if 条件的请求调用预处理代码。
- `requestCallExpr(...)`：生成请求调用表达式或 helper 调用表达式。

## src/apiinline/request.hpp

**文件功能**：声明 Request/Service API inline 接口。

**主要函数**
- `inlineRequestAPIs(...)`：对请求/服务相关 API 调用执行端口化替换。

## src/apiinline/utils.cpp

**文件功能**：提供 API inline 共用的文本处理、libclang token 化、字段 pack/unpack 和 debug map 工具。

**主要函数**
- `joinLines(...)`：将代码行拼接为完整源码字符串。
- `splitLinesKeepEnds(...)`：按行拆分源码并保留换行符。
- `uintExtractExpr(...)`：生成 `Int<N>.at<hi, lo>()` 位段访问表达式。
- `castToLvalueTypeExpr(...)`：生成旧式按目标左值类型转换的表达式。
- `packFlatFieldValueExpr(...)`：根据 `FlatField` 生成结构体字段打包到 `Int<N>` 的表达式。
- `unpackFlatFieldValueExpr(...)`：根据 `FlatField` 生成 packed 位段还原为结构体成员的表达式。
- `flatFieldValueExpr(...)`：将展平字段名映射为相对某个根对象的访问表达式。
- `enumDefaultValueExpr(...)`：计算 enum 类型的默认初始成员表达式。
- `defaultValueExprForType(...)`：生成类型默认值表达式。
- `findMatching(...)`：在 token 序列中查找匹配括号位置。
- `splitTopLevelArgs(...)`：按顶层逗号拆分调用参数。
- `tokenizeWithLibclang(...)`：使用 libclang 对源码进行 token 化。
- `applyReplacementsWithDebug(...)`：应用文本替换并同步更新 debug 行号映射。
- `normalizeTemplateLambdaCalls(...)`：识别生成的泛型 lambda，并将其 `name<...>(...)` 调用规范为 C++20 的 `name.template operator()<...>(...)`。

## src/apiinline/utils.hpp

**文件功能**：声明 API inline 共用数据结构和工具函数。

**主要函数/类型**
- `TokenInfo`：保存一个 libclang token 的文本、类型和源码区间。
- `Replacement`：描述一次源码替换的起止位置和替换文本。
- `InlineCode`：保存 inline 后代码行及其 debug 映射。
- `packFlatFieldValueExpr(...)`：声明字段打包表达式生成函数。
- `unpackFlatFieldValueExpr(...)`：声明字段解包表达式生成函数。
- `tokenizeWithLibclang(...)`：声明 libclang token 化入口。
- `normalizeTemplateLambdaCalls(...)`：声明生成泛型 lambda 调用语法的规范化入口。

## src/breakpoint.hpp

**文件功能**：解析断点表达式并将其转换为 trace 系统可使用的位串比较条件。

**主要函数/类型**
- `VulBreakConditionSpec`：表示一个断点条件中的信号、期望位串和显示值。
- `VulBreakPointSpec`：表示一个完整断点表达式及其条件列表。
- `collectTracedSignalWidths(...)`：收集已 trace 信号的运行时名称和位宽。
- `parseBreakExpression(...)`：解析单条断点表达式。
- `parseBreakSpecs(...)`：解析命令行或文件中的断点配置。
- `breakpointParseLiteralToBits(...)`：将断点字面量转换为固定宽度二进制字符串。

## src/bundlelib.cpp

**文件功能**：解析、静态化和展平 VUL 的 struct、enum、alias 类型定义。

**主要函数**
- `parseMemberDeclaration(...)`：解析结构体成员声明文本。
- `parseTypeSignature(...)`：解析类型签名并识别 `Int<N>`。
- `staticalizeBundle(...)`：将临时 bundle 定义解析为静态 bundle。
- `mergeStaticBundleLibs(...)`：合并全局和局部 bundle 库。
- `get_basic_width(...)`：计算基础类型位宽。
- `flatten_member(...)`：递归展平成 packed 字段列表。
- `flatten_bundle(...)`：将 struct、alias 或 enum 展平成 `FlatField` 列表；标量与嵌套枚举统一按实际显式/隐式成员值计算位宽，至少一位以表示零。

## src/bundlelib.h

**文件功能**：定义 VUL 类型系统中 bundle、成员、enum 和展平字段的数据结构。

**主要函数/类型**
- `VulStaticTypeSignature`：表示静态类型签名及 `Int<N>` 位宽。
- `VulStaticBundle`：表示静态 struct、enum 或 alias 定义。
- `FlatField`：表示展平后的字段路径、位宽、offset、fixint 标志和 enum 类型名。
- `flatten_type_signature(...)`：按类型签名生成展平字段列表。
- `flatten_bundle(...)`：声明 bundle 展平入口。
- `flatten_member(...)`：声明成员展平入口。

## src/configexpr.cpp

**文件功能**：实现配置表达式的词法分析、语法分析、求值和引用提取。

**主要函数**
- `tokenizeConfigValueExpression(...)`：将配置表达式拆成 token。
- `parseConfigValueExpression(...)`：将 token 序列解析为 AST。
- `evaluateConfigValueExpression(...)`：计算配置表达式 AST 的整数值。
- `parseReferencedIdentifier(...)`：提取表达式中引用的配置名。

## src/configexpr.hpp

**文件功能**：声明配置表达式解析器的数据结构和接口。

**主要函数/类型**
- `Token`：表示配置表达式 token。
- `ASTNode`：表示配置表达式 AST 节点。
- `tokenizeConfigValueExpression(...)`：声明配置表达式词法分析入口。
- `parseConfigValueExpression(...)`：声明配置表达式语法分析入口。
- `evaluateConfigValueExpression(...)`：声明配置表达式求值入口。

## src/configlib.cpp

**文件功能**：实现配置库静态化、常量表达式求值和配置合并。

**主要函数**
- `insertStaticConfig(...)`：将一个配置项插入静态配置库。
- `calculateConstexprValue(...)`：在给定配置库下计算常量表达式值。
- `mergeStaticConfigLibs(...)`：合并全局和局部静态配置。

## src/configlib.h

**文件功能**：定义配置项、配置库和配置求值接口。

**主要函数/类型**
- `VulTempConfig`：表示解析阶段的配置定义。
- `VulStaticConfigLib`：表示静态配置名到整数值的映射。
- `insertStaticConfig(...)`：声明配置静态化入口。
- `calculateConstexprValue(...)`：声明配置表达式求值入口。
- `mergeStaticConfigLibs(...)`：声明配置库合并入口。

## src/cppparse/

**文件功能**：按源码处理、基础扫描和宏条目扫描拆分，公共声明保留在 `src/cppparse.hpp`。

| 实现文件 | 主要函数与职责 |
|---|---|
| `source.cpp` | `readFileLines`、`stripComments`；保留注释处理与续行的原始行号映射。 |
| `scanning.cpp` | `findNext`、`findNextBraceBlock`、`matchMacros`、`codeblockContainsFunctionCall`；轻量扫描。 |
| `macro_entries.cpp` | `findAllMacroEntries`、`findAllMacroEntriesPreservingLinePositions`；处理嵌套参数、字面量、数字分隔符及宏体位置，词法辅助留在匿名命名空间。 |

## src/cppparse.hpp

**文件功能**：声明轻量 C++/宏源码解析工具。

**主要函数/类型**
- `TrimResult`：保存去注释后的代码和行号映射。
- `LinePosition`：表示源码行列位置。
- `MatchMacroResult`：表示一次宏匹配结果。
- `BlockResult`：表示括号代码块范围。
- `MacroEntry`：表示解析出的宏调用条目。
- `findAllMacroEntries(...)`：声明宏扫描入口。

## src/debugmap.hpp

**文件功能**：维护生成代码行到原始代码位置的调试映射。

**主要函数**
- `vulDebugAppendLine(...)`：追加一行代码及其 debug 位置。
- `vulDebugAppendLines(...)`：批量追加代码及 debug 位置。
- `vulDebugBuildGeneratedMap(...)`：构建最终行号映射。
- `vulDebugNormalize(...)`：规整代码行和 debug 映射长度。
- `vulDebugWriteMapToFile(...)`：写出 debug map 文件。

## src/errormsg.cpp

**文件功能**：实现全局错误上下文栈的压入、弹出和格式化。

**主要函数**
- `getGlobalErrorContextStack()`：返回进程内全局错误上下文栈。
- `vulGetErrorContextStack()`：读取当前错误上下文栈。
- `vulPushErrorContext(...)`：压入一层错误上下文。
- `vulPopErrorContext()`：弹出一层错误上下文。

## src/errormsg.hpp

**文件功能**：定义错误对象、异常类型和 RAII 错误上下文工具。

**主要函数/类型**
- `ErrorMsg`：保存错误码和错误消息。
- `VulException`：携带上下文栈的运行时异常。
- `VulErrorContextGuard`：进入作用域时压入错误上下文，离开时弹出。
- `EStr(...)`：构造 `ErrorMsg`。

## src/instance_graph.cpp / src/instance_graph.h

**文件功能**：共享具体实例端点展开，保留声明路径生成身份。

- `materializeConcreteConnections(...)`：绑定源坐标并求值目标索引，展开多维边界通配与子事务维度到扁平事务索引，落实隐式子服务 REQUEST 的完整形状；分析、验证和两种生成器使用相同连接结果。
- `concreteGenerationView(...)`：将具体子节点和连接转换为生成视图，不改变声明路径及坐标上下文。

## src/port_shape.cpp / src/port_shape.h

**文件功能**：共享多维事务形状与调用降低规则。

- `portCoordinates(...)` / `portSubscript(...)` / `portIndexExpression(...)`：按最后一维变化最快转换坐标、参数访问和扁平编号。
- `portArrayType(...)` / `portRequestFacade(...)`：构造嵌套数组类型，以及显式坐标／批量 REQUEST 接口；批量 ARG 取得值副本、RESP 保持引用、握手返回逐端口结果。
- `portCallSlots(...)`：静态校验调用坐标或展开全部调用端点。
- `lowerPortIndexDims(...)`：将 Service 的 `IDX_DIM` 降低为一致的坐标常量表达式。

## src/module/

**文件功能**：将临时模块静态化，分析事务调用并计算具体实例的更新顺序。公共数据模型与入口保留在 `src/module.h`。

| 实现文件 | 主要函数与职责 |
|---|---|
| `instantiate.cpp` | `instantiateModule`；求值参数、配置和局部类型，按原顺序编排各静态化阶段。 |
| `transactions.cpp` | `staticalizeReqServ`、`staticalizeTransactions`、`staticalizeTicks`；匹配 Service／QUERY 声明与实现，要求 QUERY 返回类型匹配且具有唯一实现；保留逻辑块编号与 priority。 |
| `resources.cpp` | `staticalizeResources`；静态化 Register、Wire、Queue、BRAM、ROM。 |
| `children.cpp` | `staticalizeChildren`；子实例维度、参数覆盖与 COORD 绑定。 |
| `connections.cpp` | `staticalizeConnections`；连接索引解析、通配边界连接与子接口引出；QUERY 支持多通配，记录返回值外层形状与逐实例槽位。 |
| `call_analysis.cpp` | `detectRequestCallInLogicBlocks`；`joinCode`、`transactionCallIndices`、`activeConstexprCode` 供调度阶段提取静态有效调用并保留诊断偏移。 |
| `scheduling.cpp` | `setupUpdateSequence`；建立具体事务端点调用图，诊断调用环、重复调用、多更新源及更新约束环，将 priority 投影到公共祖先。 |
| `detail.hpp` | `vulmodule::detail::InstantiationContext` 与内部阶段声明；共享目标实例、临时模块、配置／类型库和逻辑块编号。 |

具体连接仍由 `materializeConcreteConnections` 展开；事务调用维度规则复用 `port_shape`。

## src/module.h

**文件功能**：定义 VUL 模块、寄存器、请求、服务、队列、BRAM、子实例和静态实例层次的数据结构。

**主要函数/类型**
- `VulTempModule`：表示宏解析后的临时模块。
- `VulStaticModuleInstance`：表示静态化后的模块实例。
- `VulStaticReqServ`：表示静态 request/service 定义。
- `VulStaticRegister`：表示静态寄存器定义。
- `VulStaticQueue`：表示静态队列定义。
- `instantiateModule(...)`：声明模块实例化入口。
- `setupUpdateSequence(...)`：声明更新顺序构建入口。

## src/project.h

**文件功能**：定义完整 VUL 工程的静态表示。

**主要类型**
- `VulStaticProject`：保存顶层模块、全局配置、全局 bundle、全局 helper 和测试模块。

## src/rtlgen/

**文件功能**：生成 RTL skeleton、经 API inline 的 HLS logic 和 Verilator 测试桥接。公共入口保留在 `src/rtlgen.h`。

| 实现文件 | 主要函数与职责 |
|---|---|
| `module.cpp` | `genModuleRTL`；建立具体生成视图、编排处理阶段、组装输出和调试映射。 |
| `declarations.cpp` | `_procConstAndBundle`、`_procWires`；常量、类型、helper 和 wire。 |
| `registers.cpp` | `_procRegisters`；寄存器端口、RTL 实例、零初始化及用户复位块。 |
| `transactions.cpp` | `_procRequests`、`_procServicesAndTicks`、`_procQueries`；事务端口、逻辑入口及 priority。 |
| `connections.cpp` | `_procChildrenAndConnection`；子模块线网、Service／Query 引出与转发；通配 QUERY 生成非模板快照 helper，逐实例解包到返回数组。 |
| `queues.cpp` | `_procQueues`；Queue／QueueMP 端口及 RTL 实例，计数输入使用硬件计数位宽。 |
| `memories.cpp` | `_procBRAMAndROM`；存储器端口、实例及 ROM 资源文件。 |
| `verilator.cpp` | `genVerilatorTestMainCpp`；测试绑定、数组事务与复位，宽端口按 Verilator 存储布局转换。 |
| `common.hpp` / `common.cpp` | `ArgPort`、端口命名、字段表达式和参数展平；仅声明跨文件辅助接口。 |
| `detail.hpp` | `rtlgen::detail::RTLGenContext`、内部处理阶段；集中持有代码缓冲区和调试映射。 |

**顺序约束**：先收集事务／连接／资源逻辑并检查 Register API 使用情况，再处理寄存器；API inline 完成后插入寄存器复位计算。文件级全局端口与方向 pragma 的 ABI 保持一致。RTLZZ 接入仍由根目录的 `rtlgen_rtlzz.cpp` 和 `rtlzz_bridge.cpp` 实现。

## src/rtlgen.h

**文件功能**：声明 RTLGen 结果结构和 RTL 生成入口。

**主要函数/类型**
- `RTLGenResult`：保存 skeleton、logic、debug map 和资源文件列表。
- `LogicSubModuleName(...)`：生成逻辑子模块名称。
- `genModuleRTL(...)`：声明唯一的 RTL skeleton/APIInline 生成入口。
- `appendLogicRTL(...)`：声明调用 RTLZZ 追加 logic RTL 的入口。
- `genVerilatorTestMainCpp(...)`：声明 Verilator 测试代码生成入口。

## src/rtlgen_rtlzz.cpp

**文件功能**：将 RTLGen 生成并经 APIInline 处理的 logic C++ 交给 RTLZZ 转换为 RTL并追加到 skeleton。

**主要函数**
- `appendLogicRTL(...)`：调用 RTLZZ 生成 logic RTL 并追加到 `RTLGenResult`。
- `appendTextAsLines(...)`：将 RTLZZ 输出文本追加为代码行。

## src/rtlzz_bridge.cpp

**文件功能**：封装对 RTLZZ 编译接口的调用，并将其进度事件呈现为 stdout 上的单行动态状态。

**主要函数**
- `generateLogicRTLWithRTLzz(...)`：读取 logic C++，为 RTLZZ 安装带模块名的进度回调，以 `\r` 原地刷新当前阶段/优化轮次，结束或异常时收尾换行，并生成 SystemVerilog 文本。

## src/rtlzz_bridge.hpp

**文件功能**：声明 RTLZZ 桥接函数。

**主要函数**
- `generateLogicRTLWithRTLzz(...)`：声明 logic C++ 到 RTL 文本的转换入口。

## src/simgen/

**文件功能**：生成 C++ 仿真模块、工程头文件和测试 harness。公共入口与结果结构保留在 `src/simgen.h`。

| 实现文件 | 主要函数与职责 |
|---|---|
| `common.cpp` | `genCurrentTimeString`、内部 `genHeaderPrelude` 与共享子实例命名辅助。 |
| `headers.cpp` | `genStaticConfigHeaderCode`、`genStaticBundle`、`genStaticBundleHeaderCode`、`genStaticGlobalHelperHeaderCode`、`genStaticProjectHeaderCode`。 |
| `module.cpp` | `genStaticModuleCodeHpp`；局部声明、Tick／helper、功能阶段编排及最终声明／实现组装。 |
| `transactions.cpp` | `emitTransactions`；Request、Service、Query 包装及边界 Service 转发。 |
| `resources.cpp` | `emitResources`；状态组件、复位值零初始化、用户复位、周期提交与资源清单。 |
| `hierarchy.cpp` | `emitHierarchy`；具体子实例类型、坐标模板、请求连接与子接口引出；通配 QUERY 生成无参快照函数，以通配维度嵌套返回 std::array。 |
| `tracing.cpp` | `emitTracing`；trace 注册、阵列坐标过滤和周期采样。 |
| `harness.cpp` | `genStaticTestHarnessCodeHpp`、`genStaticTestHarnessHpp`、`genStaticTestMainHpp`；测试绑定、trace／断点集成及 main。 |
| `detail.hpp` | `simgen::detail::ModuleGenContext`；持有完整具体生成视图、类名及各代码段／调试映射，生命周期覆盖所有阶段。 |

RTL 与仿真继续共用现有 bundle 生成接口，没有新增公共生成框架。

## src/simgen.h

**文件功能**：声明仿真代码生成接口和生成结果结构。

**主要函数/类型**
- `StaticModuleCodeHpp`：保存模块声明/实现代码及 debug map。
- `StaticTestHarnessCodeHpp`：保存测试 harness 声明/实现代码。
- `genStaticModuleCodeHpp(...)`：声明模块仿真代码生成入口。
- `genStaticTestHarnessCodeHpp(...)`：声明测试 harness 代码生成入口。
- `genStaticTestMainHpp(...)`：声明 main 代码生成入口。

## src/stringop.hpp

**文件功能**：提供字符串 trim、空白跳过和 split 工具。

**主要函数**
- `ltrim_inplace(...)`：原地移除左侧空白。
- `rtrim_inplace(...)`：原地移除右侧空白。
- `trim_inplace(...)`：原地移除两侧空白。
- `trim(...)`：返回去除两侧空白的新字符串。
- `skip_spaces(...)`：从指定位置跳过空白字符。
- `split(...)`：按字符或字符串分隔符拆分字符串。

## src/toposort.hpp

**文件功能**：提供带诊断信息的有向图拓扑排序工具。

**主要函数/类型**
- `TopoGraph`：维护节点、边和拓扑排序状态。
- `addNode(...)`：加入排序节点。
- `addEdge(...)`：加入依赖边。
- `sort()`：执行拓扑排序并检测环。
- `getSorted()`：返回排序结果。
- `getCycle()`：返回检测到的环路径。

## src/trace.cpp

**文件功能**：解析 trace 规则并为每个模块实例选择需要记录的展平信号。

**主要函数**
- `parseTraceMatcher(...)`：解析单条 trace 匹配表达式。
- `parseTraceOptions(...)`：根据工程层次和匹配规则生成 trace 表。
- `matchPathSegments(...)`：按路径段和通配符匹配实例或信号路径。
- `extractInstanceIndexFilter(...)`：解析数组实例 trace 的索引过滤条件。

## src/trace.hpp

**文件功能**：定义 trace 匹配规则、trace 信号和 trace 表。

**主要函数/类型**
- `VulTraceMatcher`：表示实例路径和信号路径匹配规则。
- `VulTracedSignal`：表示一个被 trace 的展平信号及其位宽和实例过滤。
- `VulTraceTable`：表示实例 ID 到 trace 信号列表的映射。
- `parseTraceMatcher(...)`：声明 trace 规则解析入口。
- `parseTraceOptions(...)`：声明 trace 表生成入口。

## src/type.cpp

**文件功能**：实现标识符合法性检查、标识符替换和基础类型判断。

**主要函数**
- `isValidIdentifier(...)`：检查字符串是否为合法且非保留的标识符。
- `identifierReplace(...)`：按标识符边界替换名称。
- `isBasicVulType(...)`：判断类型名是否为 VUL 基础类型。

## src/type.h

**文件功能**：定义基础别名和类型检查工具接口。

**主要函数/类型**
- `Comment`：表示源码注释文本。
- `DataType`：表示类型名字符串。
- `isValidIdentifier(...)`：声明标识符合法性检查。
- `identifierReplace(...)`：声明标识符替换工具。
- `isBasicVulType(...)`：声明基础类型判断工具。

## src/vcpp.cpp

**文件功能**：保留工程解析公共入口。

- `parseVcppStaticProject(...)`：转调 `vulcpp/project_parser.cpp` 中的 `parseVcppStaticProjectImpl(...)`。
- 模块／测试宏处理器分别位于 `vulcpp/module_handlers.*` 和 `vulcpp/test_handlers.*`。

## src/vcpp.hpp

**文件功能**：声明 VUL C++ 工程解析入口。

**主要函数**
- `parseVcppStaticProject(...)`：从工程目录、顶层模块和测试文件解析静态工程。

## src/vullib.hpp

**文件功能**：定义生成输出时需要复制的 VUL runtime 文件清单。

**主要函数/类型**
- `getVulLibFiles()`：返回 runtime 头文件和资源文件列表。

## src/vulcpp/declaration_args.cpp / declaration_args.hpp

**文件功能**：声明 API 的共享参数规格与归一化入口。

- `normalizeDeclaration(...)`：统一名称／类型／属性的固定顺序及显式命名，拒绝重复参数、位置／命名混用错误、旧宏和同义属性；输出处理器所需的位置前缀与命名属性，保留隐式握手和原始源码位置。
- 全局 header、模块、版本化接口／实现、测试入口与 TOP／PROJECT 路径扫描共享该入口；`common.cpp` 负责顶层赋值识别与方括号维度列表校验。
- 用户语法以 `doc/userguide/0-declarations.md` 为参数表；`vullib/defhelper.hpp` 仅覆盖基础位置写法的编辑期类型辅助。
- `regression/declaration_api.py` 验证参数语法、移除诊断及 C++20 编辑期声明；`regression/generator_declaration_api.py` 验证完整生成流程。

## src/vulcpp/project_parser.cpp

**文件功能**：读取 VULC++ 工程、递归实例化模块，并建立仿真 harness 与顶层的连接。

- `parseVcppStaticProjectImpl(...)`：解析工程并逐坐标展开具体实例，落实 COORD 绑定和普通参数覆盖，保留声明路径用于共享模板文件；构建用于更新顺序分析的 TestMain；子 Request 驱动的顶层内部 Service 不添加虚构的外部调用源。

## QUERY 声明与快照回归

`regression/generator_query_api.py` 覆盖 INTERFACE 必需声明、返回类型匹配与缺失／重复实现；验证 simgen 与 RTL/API inline 中多维、部分通配、数组返回嵌套、结构体、枚举、宽 Int、复位与快照值副本。QUERY 本身始终是普通无参函数，阵列只属于通配引出返回值。
