#pragma once
#include <atomic>

// 仅测试使用的故障注入，不是 Camera 硬件模拟或性能测量。
enum class TestCaptureMode { Frames, Timeout, InitFailure, InitException, CaptureError, CaptureException };
extern std::atomic<TestCaptureMode> test_capture_mode;
extern std::atomic<int> test_capture_calls;
