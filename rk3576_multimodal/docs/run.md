# 构建与运行

## 当前可用范围

核心结构和 Windows 平台无关测试已实现。真实 LLM/VLM 后端缺少 SDK 和模型，
默认会输出 `RK3576 Runtime/SDK not available`，不会给出假 AI 答案。
使用默认选项可以构建 CLI、Router、Engine、最新帧缓存和 CameraService。

用户提供的旧版单帧 IMX415 验证结论仍作为硬件基线：
/dev/video11，3840×2160 NV12，VIDEO_CAPTURE_MPLANE，MMAP，
num_planes=1，bytesused=12441600。本次新增 Camera 行为 **RK3576 实机待验证**。

## RK3576 环境要求

- RK3576 Linux、IMX415 与已经配置好的 media/ISP 链路，/dev/video11 可访问。
- CMake >= 3.16；支持 C++17 和 std::filesystem 的编译器（建议 GCC >= 9）。
- 标准 C++ 库、POSIX threads、Linux V4L2 用户态头文件；当前不依赖 OpenCV/Qt。
- 原生编译可用 Make/Ninja；交叉编译需自行提供真实 aarch64 Linux toolchain/sysroot。
- 真实推理还需与固件/驱动匹配的 RKLLM、视觉编码 Runtime/SDK 及模型；
  具体版本尚未确定，不宣称任意 RKNN/RKLLM 组合可用。

## RK3576 原生编译（实机待执行）

在工程根目录：

```sh
cmake -S . -B build/rk3576 -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_WITH_RKLLM=OFF -DBUILD_WITH_VLM=OFF
cmake --build build/rk3576 --parallel 4
ctest --test-dir build/rk3576 --output-on-failure
```

以上测试包含可移植逻辑与测试专用设备故障注入，不替代真实硬件测试。
关闭 BUILD_TESTING 可省略测试目标。Runtime 开关 OFF 仍构建 Engine 与明确报错的后端。

## RK3576 运行（实机待执行）

```sh
./build/rk3576/rk3576_multimodal --device /dev/video11
```

CLI 命令：

```text
User> 你是谁
Request Type: TEXT
Error> RK3576 Runtime/SDK not available: ...
User> 前面有什么
Request Type: VISION
Frame ID: ...  Frame Timestamp(ns): ...  Frame Age: ... ms
Error> RK3576 Runtime/SDK not available: ...
User> stats
User> quit
```

以上是输出形式说明，不是实机运行记录；如果尚未取得帧，视觉请求会输出
`No latest camera frame available`。可以用 exit、quit 或 EOF 安全退出。
Linux CLI 等待输入期间支持 SIGINT/SIGTERM；同步推理进行中需等待 backend 返回。
摄像头启动失败时程序保留 CLI 供文本请求和诊断，进程退出码仍可为 0；
不要用退出码 0 判断模型或 Camera 已初始化成功。

## Windows 本地验证

本机实际使用 MinGW g++ 16.1.0、CMake 4.3.2 和 MinGW Makefiles。
Ninja 1.13.2 在本机最初的编译器探测中停滞，改用下面生成器成功；原因未确认。

```powershell
cmake -S . -B build/windows-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/windows-mingw --parallel 4
ctest --test-dir build/windows-mingw --output-on-failure
.\build\windows-mingw\rk3576_multimodal.exe --no-camera
```

Windows 选择 video_capture_unavailable.cpp，只明确报告 Camera 不可用，不生成帧。
Linux 自动选择原来的 video_capture.cpp。Windows 成功构建不等于 V4L2 已编译验证。
CLI 输入使用 UTF-8；Windows 交互控制台暂时切为 UTF-8，退出恢复。
PowerShell 管道编码取决于 shell，自动化测试直接使用 UTF-8 INPUT_FILE。
Windows 使用 exit/quit/EOF；不将强制结束进程描述为正常资源释放验证。

## 模型路径与 Runtime

模型参数独立配置：

```sh
./build/rk3576/rk3576_multimodal --device /dev/video11 \
  --llm-model /opt/models/text/model.rkllm \
  --vlm-model /opt/models/vision-language/model.rkllm \
  --vision-model /opt/models/vision/encoder.rknn
```

这些只是路径示例，不承诺该文件类型组合被任何现有后端支持。
当前没有模型格式解析，也没有 NV12 到模型输入的真实预处理。
指定不存在的路径会报告 Model file；路径存在仍会报告 Runtime/SDK not available。
`--no-camera` 适用于纯文本业务验证；`--help` 列出参数。

真实接入前仍需：

1. RK3576 支持的 RKLLM C/C++ 头文件、aarch64 运行库、官方 demo、模型转换工具与版本信息。
2. 支持目标 VLM 的视觉编码器 Runtime（可能涉及 RKNN/RKNPU）、
   头文件、运行库、匹配的板端驱动和官方 multimodal 示例。
3. 匹配 Runtime 的量化 LLM/VLM/视觉模型、tokenizer、prompt 模板、视觉 token/embedding 接口。
4. 模型图像尺寸、NV12 颜色转换、stride、resize/crop/normalize 等预处理规格。
5. 将 src/ai/rk_backends_unavailable.cpp 替换为基于真实 SDK 的实现，
   更新 CMake 的 include/link 检查与来源选择，再允许 Runtime 开关 ON。

目前 `BUILD_WITH_RKLLM=ON` 或 `BUILD_WITH_VLM=ON` 都会有意使 CMake 配置失败。
这两个开关是诚实的集成门槛，不是已经可用的 SDK 搜索功能。
不下载 SDK/模型，不调用 OpenAI/DashScope/其他云 API。
模型、build、sdk、runtime 目录及常见模型/运行库扩展名已加入 .gitignore。

## 常见错误

| 错误 | 说明与排查 |
|---|---|
| open /dev/video11 | 检查节点、权限、驱动、设备占用和 media 链路 |
| VIDIOC_QUERYCAP / 不支持 MPLANE | 检查是否选择了正确 capture 节点 |
| VIDIOC_S_FMT | 检查格式/分辨率；程序使用驱动协商的最终参数 |
| VIDIOC_REQBUFS / QUERYBUF / mmap | 检查驱动缓冲区支持与内存；日志带操作/缓冲编号 |
| VIDIOC_STREAMON | 检查 sensor、ISP/media 配置和驱动日志 |
| poll / DQBUF / QBUF | 查询 stats.last_error；fatal 错误后停止采集并清空最新帧 |
| timeout_count 增长 | 当前没有新帧；不逐次打印。旧帧可能变陈旧，观察 frame age |
| No latest camera frame | 尚未首帧、Camera 未启动、出错或已停止；稍后再请求并查询 stats |
| Runtime/SDK not available | 默认行为；需要真实 SDK 和 adapter，不能靠路径参数解决 |
| Model file does not exist | 检查传入模型是否存在且是普通文件 |
| initialization/inference exception | Engine 捕获后端异常并返回文字诊断；检查 SDK/模型兼容性 |
| thread start exception | 检查系统线程资源；程序清理部分初始化状态并保留错误 |
| Missing value / Unknown argument | CLI 参数无效，退出码 2 |

## 必须补做的 RK3576 验证

- Linux/aarch64 真实编译和链接（Windows 未编译 Linux 源文件）。
- /dev/video11 非阻塞打开、poll、DQBUF/QBUF 与原 MMAP 基线兼容性。
- 连续采集、画面内容、协商 stride/plane_sizes、帧时间戳。
- 无帧、断流、设备异常、重复启停、SIGINT/SIGTERM 与退出延迟。
- VLM 慢消费者期间真实 Camera 是否持续更新，内存占用是否稳定。
- 真实 Camera FPS、CPU copy 成本、VLM 帧龄和 LLM/VLM 延迟。
- SDK 集成后的模型初始化、预处理、真实回答、内存/NPU 分配与释放。
- SDK 支持后再测 TTFT/tokens/s；当前不存在这两项实测数据。

详见 validation.md 的本地证据与未验证边界。
