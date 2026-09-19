# RK3576 多模态核心架构

## 状态与目标

目标是端侧 Camera + LatestFrame + LLM + VLM + Router + CLI + Profiling。
当前已实现业务核心、线程管理和可替换接口；**真实 LLM/VLM 推理尚未实现**，
因为本工程没有 RKLLM/RKNN/VLM SDK、运行库、官方 demo 或模型。默认后端明确失败：
`RK3576 Runtime/SDK not available`。不生成假答案，不调用云端。

维护者提供的旧版实机基线：RK3576 + IMX415，/dev/video11，
V4L2 VIDEO_CAPTURE_MPLANE + MMAP，3840×2160 NV12，num_planes=1，
bytesused=12441600，单帧采集成功。本次新增持续采集、非阻塞 poll、
线程停止和所有 AI 硬件行为均为 **RK3576 实机待验证**。

## 数据流与职责

```text
IMX415 -> /dev/video11 -> Linux V4L2 / MMAP
                         -> VideoCapture -> ImageFrame（一次 CPU copy）
                         -> CameraService 采集线程
                         -> LatestFrameBuffer（单槽 shared_ptr<const ImageFrame>）

CLI 文本 -> RequestProcessor -> IntentRouter
  TEXT   -> LLMEngine -> ILLMBackend -> RKLLMBackend -> InferenceResult
  VISION -> LatestFrameBuffer::getLatest()
         -> VLMEngine(frame, question) -> IVLMBackend -> RK3576VLMBackend
         -> InferenceResult -> CLI 文本输出
```

- main：参数读取、对象组装、初始化与退出。
- cli：输入输出、命令、格式化统计；不直接调用 V4L2 或 Runtime API。
- RequestProcessor：统一业务入口，后续 ASR、Qt 可以复用。
- IntentRouter：可解释 UTF-8 子串规则，返回 Text/Vision 和命中原因。
  例如“前面有什么”“当前画面”“摄像头里”进入 Vision；无规则命中默认为 Text。
  “摄像头的工作原理是什么”走 Text。规则不能理解否定、指代和复杂语义。
- VideoCapture：保留原来的 V4L2 初始化、MMAP、拷贝、QBUF、释放路径。
  增加非阻塞打开、poll、错误描述、缓冲边界检查和布局信息。
- CameraService：线程、启停、错误状态和统计；不调用任何 AI 模块。

## 线程模型与生命周期

主线程同步执行 CLI 和一次推理。CameraService 独立线程持续采集；
不会每帧触发 VLM，也不会把视频帧排队送入 AI。

`running_` 是 atomic，供主线程和采集线程安全交换停止状态。
生命周期 mutex 串行化 start/stop，采集线程不持有它。
统计使用独立 mutex，锁内只更新/复制计数与状态。模型实例各用自己的 mutex
串行化 initialize/infer/shutdown；模型的长推理锁与 Camera/帧缓存锁完全独立。

设备以 O_NONBLOCK 打开，poll 等待最多 200 ms 后允许线程重新检查 running。
poll 超时、EINTR、DQBUF EAGAIN 作为可重试状态；其他错误停止采集并保留诊断。
stop 设置 running=false，join 等待线程退出，随后释放设备并清空最新帧。
不从主线程并发 close 或 munmap 正在使用的设备。正常结束、错误结束和析构均会清理。
已退出但仍 joinable 的线程会在下次 start 前回收。stop 可重复调用。

200 ms 是 poll 等待上限，**不是实测退出耗时或整个驱动操作的硬实时保证**。
当前 copy、驱动 ioctl、调度和资源回收仍可能增加时间，需板端验证。
线程创建失败、初始化异常、采集异常会转成 CameraStats.last_error。
CLI 在提示符边界显示新错误，stats 随时可查询最近错误；不逐帧打日志。
摄像头失败不会终止文本 CLI；视觉请求无帧时明确失败。

对象顺序是 frames -> camera -> engines -> processor。frames 比 camera 活得久。
正常退出显式停止 Camera、关闭 Engine；异常由 RAII 析构清理。
各模块不得在析构期间仍被外部线程调用。推理为同步接口，未来真实 Runtime
必须保证 shutdown 幂等且 noexcept，且不得在 infer 返回后使用裸帧引用。

Linux CLI 使用 stdin poll/read，SIGINT/SIGTERM 的 handler 只设置 sig_atomic_t；
退出流程在主线程完成。信号不能强制取消未来的同步 Runtime 推理，
只能在其返回后退出；初始化阶段尚未安装 CLI 信号处理器。
Windows 验证使用 exit/quit/EOF 正常退出，不承诺控制台强制关闭的 RAII 行为。

## 为什么只保留最新帧

Camera 可能高频更新，VLM 一次可能数秒。FIFO 会不断增加历史图像延迟和内存。
LatestFrameBuffer 只保留一个最新 shared_ptr，不积累历史帧。
getLatest 在锁内复制 shared_ptr 后立即解锁，再进行 VLM 推理。
update 在锁内 swap 指针，旧帧可能触发的大块内存释放在锁外发生。

消费者拿到 shared_ptr<const ImageFrame> 后，即使生产者连续替换最新帧，
旧帧也会一直有效，直到最后一个消费者释放。const 防止消费者改图。
生产者完成一帧后使用 make_shared<const ImageFrame>(std::move(frame))，
移动 vector 所有权，不增加一份整图复制。消费者不应保留无界数量的旧指针。

## ImageFrame 与一次 CPU copy

ImageFrame 保留 data、width、height、pixel_format、timestamp_ns、frame_id，
增加 received_at、plane_strides、plane_sizes。多平面有效数据按顺序拼接到 data，
保留每平面行跨度与数据大小供未来预处理使用。

当前数据路径为：
V4L2 mmap buffer -> CPU copy -> ImageFrame::data -> QBUF。
QBUF 后驱动会复用 mmap buffer，因此 VLM 不能长期持有裸 mmap 地址。
这份 copy 是当前正确性设计，不是漏掉的优化。

未来 DMA-BUF/零拷贝改动点在 VideoCapture 输出、帧所有权表示、
VLM 的预处理/视觉编码器输入之间。需要明确驱动 buffer 归还时机、
消费者寿命、同步和 RGA/Runtime 导入接口。当前不实现 DMA-BUF、RGA 或零拷贝。

## Engine、Backend 与错误

ILLMBackend/IVLMBackend 是唯一 Runtime 适配层；业务层没有 Rockchip API。
LLMEngine/VLMEngine 持有 unique_ptr（独占后端所有权），管理初始化、重新初始化、
关闭、异常转换、串行访问和 latency。VLMEngine 还检查帧并记录视觉元信息。
指定的模型路径先检查是否为存在的普通文件；是否必填、格式与版本兼容性
必须由将来的真实 backend 校验，空路径不会使当前不可用后端初始化成功。

当前 RKLLMBackend/RK3576VLMBackend 位于 rk_backends_unavailable.cpp。
该文件是明确的“不支持”实现，不是可运行 Runtime 包装器。
实现真实后端前需要 SDK 头文件、库、官方示例、匹配模型以及预处理规格。
BUILD_WITH_RKLLM/BUILD_WITH_VLM=ON 当前在 CMake 配置时失败，避免误报支持。

InferenceResult 包含 success、text、error_message、latency_ms、
frame_id、frame_timestamp_ns、frame_age_ms。失败时不输出 text。
未来可向结果增加 optional TTFT/token 数/生成速率，向请求增加 system prompt、
history，或增加独立 streaming 方法；当前没有未经测量的字段值。

## Profiling 定义

- Camera frame_count：本次 start 后实际发布帧数；start 重置。
- approximate_fps：发布帧数 / 自启动至当前（或停止时）的 steady_clock 时间；
  是进程内累计平均值，不是传感器规格，也不是本次测得的 RK3576 FPS。
- timeout_count：poll 超时及可重试状态计数，不代表掉帧数。
- frame_id：VideoCapture 对象内的自增序号，不是驱动 sequence。
- frame_timestamp_ns：保留驱动 timeval 转换值，不假设它与 wall clock 同源。
- frame_age_ms：VLM 请求入口与 DQBUF 后 received_at 的 steady_clock 差，
  包含本地 copy/存储时间；不代表从传感器曝光开始的端到端延迟。
  无本地时间返回 -1；无帧返回 id=0、timestamp=0、age=-1。
- latency_ms：Engine 方法入口到结果的实际墙钟耗时（steady_clock），包括验证、
  Engine 锁等待及 backend 调用。错误路径也计时，不能当作模型推理性能。
- 不输出 TTFT 或 tokens/s，当前 Runtime 不提供这些数据。

## 后续接口位置（本次不实现）

| 模块 | 接入位置 |
|---|---|
| ASR | 把识别出的文本交给 RequestProcessor::process |
| TTS | 消费成功的 InferenceResult.text |
| YOLO | 独立消费者读取 LatestFrameBuffer，自己持有 shared_ptr；避免占住缓存锁 |
| Qt | 替换 CLI 展示层，在 UI 外的工作线程调用 RequestProcessor；UI 显示结果 |
| 工具/控制功能 | 扩展 RequestType 与 RequestProcessor，保持 Camera 与 Runtime 解耦 |
| DMA-BUF/zero-copy | VideoCapture 帧输出、帧资源所有权和 VLM 预处理接口 |
