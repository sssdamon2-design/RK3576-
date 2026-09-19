# 本次实现与验证记录

## 验证边界

日期：2026-09-19。运行环境：Windows 本地工作区；没有连接 RK3576。
工程初始仅包含 9 个源/头/CMake 文件；在工程范围的文件与文件名检查中，
未找到 RKLLM、RKNN、RKNPU SDK、模型、运行库或官方 demo。
没有执行 Git commit/push/reset，也没有下载第三方依赖或模型。
所有构建产物位于被忽略的 build/，没有加入版本控制。

用户先前提供的实机单帧采集基线不作为本次修改已经通过的硬件证据。

## Milestone 清单与逐项复核

表中“通过”限于本地编译、测试或源码检查，不能解释为板端运行通过。
所有涉及 V4L2、/dev/video11、RKLLM、RKNN/RKNPU、模型推理和硬件性能：
**RK3576 实机待验证**。

| Milestone | 修改/新增文件 | 依赖、线程/生命周期、错误退出、CMake 与验证 |
|---|---|---|
| 1 后台 Camera | include/video/camera_service.h、src/video/camera_service.cpp、include/video/video_capture.h、src/video/video_capture.cpp、CMakeLists.txt、src/main.cpp | Threads；CameraService 加入目标；生命周期锁与采集线程不互锁；poll+非阻塞 DQBUF；重复启停、错误退出、析构 join 使用测试设备替身验证。Linux 驱动路径只做源码审查 |
| 2 公共帧 | include/common/types.h、include/video/latest_frame_buffer.h、src/video/latest_frame_buffer.cpp、include/common/timing.h | 标准库；const shared_ptr、锁内交换/锁外销毁；慢消费者期间更新及旧帧寿命测试通过；保留 CPU copy；新增 steady_clock 时刻与平面布局 |
| 3 公共结果 | include/common/inference_result.h | 简单值类型，无外部资源；含成功/错误/latency/帧元数据；无帧和异常结果测试通过 |
| 4 LLMEngine | include/ai/llm_engine.h、src/ai/llm_engine.cpp、src/ai/model_validation.h | 标准库/filesystem；独占 backend，模型调用串行；初始化失败清理、空 backend、缺模型、推理异常、shutdown 后调用已测 |
| 5 VLMEngine | include/ai/vlm_engine.h、src/ai/vlm_engine.cpp | 只由视觉请求调用；共享帧在推理期间有效；无帧/空帧/异常/帧号和帧龄已测；不持有缓存锁 |
| 6 Backend | include/ai/backend_config.h、include/ai/llm_backend.h、include/ai/vlm_backend.h、include/ai/rk_backends.h、src/ai/rk_backends_unavailable.cpp | SDK API 不进入业务层；shutdown 契约为幂等 noexcept；未增加第三方依赖；不可用实现从不返回成功答案 |
| 7 Runtime 检查 | src/ai/rk_backends_unavailable.cpp、CMakeLists.txt、docs/run.md | 工程无 SDK/模型；默认缺失错误已测；两个 Runtime option 分别开启均按预期配置失败；真实 adapter 未实现 |
| 8 Router | include/router/intent_router.h、src/router/intent_router.cpp | 无外部依赖与可变共享状态；所给中文文本/视觉例句及英文大小写规则已测；未引入分类 LLM |
| 9 CLI | include/app/cli.h、src/app/cli.cpp、include/app/request_processor.h、src/app/request_processor.cpp、src/main.cpp | 组装与输入输出分离；单主线程同步请求；Camera 独立运行；quit/exit/EOF、错误参数、缺模型、无 Camera、无 Runtime 路径已测；Linux 信号分支源码审查 |
| 10 Profiling | include/common/timing.h、include/common/inference_result.h、include/video/camera_service.h、src/video/camera_service.cpp、src/ai/llm_engine.cpp、src/ai/vlm_engine.cpp、src/app/cli.cpp | steady_clock；统计锁不包围采集或推理；查询输出避免逐帧日志；只有本地计时/计数，无伪造 FPS/TTFT/tokens/s |
| 11 CMake | CMakeLists.txt、src/video/video_capture_unavailable.cpp | C++17、Threads；core/camera/CLI 分层；Windows 配置/编译/链接通过；Linux 选择原 V4L2 文件，尚未编译验证 |
| 12 工程结构 | include/ai、include/app、include/router、src/ai、src/app、src/router、tests、config/README.md、.gitignore | 未删除原 Camera 文件；测试替身只进入测试可执行文件；build/SDK/模型忽略；所有新增 .cpp 归属已检查 |
| 13 文档 | docs/architecture.md、docs/run.md、docs/validation.md | 记录架构、依赖、线程、错误、扩展点、命令和实机边界；文档不增加运行时依赖 |

## Windows 实际执行结果

工具版本：CMake 4.3.2、MinGW g++ 16.1.0、MinGW Makefiles。
最初 Ninja 1.13.2 在编译器探测阶段停滞，终止了本次启动的进程后更换生成器；
原因未确认，没有将其归因于项目代码。

执行：

```powershell
cmake -S . -B build/windows-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/windows-mingw --parallel 4
ctest --test-dir build/windows-mingw --output-on-failure
```

配置、构建与链接成功。编译启用 -Wall -Wextra -Wpedantic；
首次出现 NOMINMAX 重定义警告，已增加条件保护，后续构建无该警告。
最终 CTest 结果：**3/3 通过**。

| 测试 | 证据范围 |
|---|---|
| core_tests | Router 分类；缺 Runtime/缺模型/空 backend；初始化与推理异常；无帧/空帧；帧元信息；只路由到指定 Engine；模拟慢消费者期间生产者更新；旧 shared_ptr 生命周期 |
| camera_service_tests | 测试设备故障注入：初始化失败/异常、持续发布、重复 start/stop、超时等待后停止、采集错误/异常、错误后的重新启动、析构 join |
| cli_smoke | UTF-8 中文 TEXT/VISION 路由；缺 Runtime 不输出 Assistant 答案；无帧错误；stats；quit/exit/EOF；帮助与缺参数；缺模型；Windows 默认 Camera 不可用诊断 |

测试设备替身不链接产品目标，不验证 Linux fd、mmap 或真实硬件。
AI 测试替身只注入初始化状态、延迟与错误，不返回模拟 AI 答案。
线程创建失败的 catch 路径已审查，未强制耗尽系统线程资源来触发真实创建失败。
没有运行线程 sanitizer；测试通过不等于所有调度情况都已穷尽。

额外验证：

- BUILD_WITH_RKLLM=ON：配置按预期失败，报告 Runtime/SDK not available。
- BUILD_WITH_VLM=ON：配置按预期失败，报告 Runtime/SDK not available。
- git diff --check：无补丁空白错误；Git 提示当前工作副本存在 LF/CRLF 转换设置。
- 源码及 CMake 审查：Linux 目标选择 video_capture.cpp；Windows 选择不可用实现。
- 没有实际调用 /dev/video11、V4L2、Rockchip SDK 或模型。

## 已知限制与实机待验证

1. **LLM/VLM 真实推理尚未实现**；需要真实 SDK、可确认 API、模型和视觉预处理规格。
2. Linux V4L2 源码、CLI poll/signal 分支没有在本次 Windows 环境编译/运行。
3. 200 ms 是 poll 等待上限；真实停流/退出延迟仍可能受驱动、copy 与调度影响。
4. 帧龄从本地 DQBUF 后计时，不等于曝光到回答的端到端延迟。
5. Camera 仅超时而未 fatal error 时会保留上一帧；CLI 会显示其增长的帧龄，
   当前不自动重连，也没有配置化最大帧龄拒绝策略。
6. Router 是规则匹配，可能误判否定、指代或未收录的表达。
7. CLI 同步处理请求；未来 Runtime 推理没有取消接口，退出需要等待其返回。
8. Windows 控制台强制关闭、SIGKILL 等不属于正常 RAII 清理保证。
9. stats FPS 为软件累计平均值；没有声称新增代码在实机达到 30 FPS。
10. ASR、TTS、YOLO、Qt、RGA、DMA-BUF、zero-copy 均未实现。
