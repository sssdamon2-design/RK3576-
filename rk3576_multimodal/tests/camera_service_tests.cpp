#include "camera_test_capture.h"
#include "video/camera_service.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool waitForStopped(CameraService& camera)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (camera.isRunning() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return !camera.isRunning();
}
}
int main()
{
    try
    {
        LatestFrameBuffer frames;
        CameraService camera("test-only", 2, 2, 0, frames);
        camera.stop(); camera.stop();
        test_capture_mode = TestCaptureMode::InitFailure;
        check(!camera.start() && camera.stats().last_error.find("open failure") != std::string::npos, "init failure");
        test_capture_mode = TestCaptureMode::InitException;
        check(!camera.start() && !camera.stats().last_error.empty(), "init exception");
        test_capture_mode = TestCaptureMode::Frames;
        check(camera.start() && camera.start(), "repeat start");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (camera.stats().frame_count < 3 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto held = frames.getLatest();
        check(held && camera.stats().frame_count >= 3, "continuous publish");
        camera.stop(); camera.stop();
        check(!camera.isRunning() && !frames.getLatest() && !held->data.empty(), "stop/held frame");
        test_capture_mode = TestCaptureMode::Timeout;
        test_capture_calls = 0;
        check(camera.start(), "restart");
        const auto timeout_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (test_capture_calls.load() == 0 && std::chrono::steady_clock::now() < timeout_deadline)
            std::this_thread::yield();
        const auto before_stop = std::chrono::steady_clock::now();
        camera.stop();
        check(std::chrono::steady_clock::now() - before_stop < std::chrono::seconds(1), "timeout stop unbounded");
        check(camera.stats().timeout_count > 0, "timeout statistics");
        for (auto mode : {TestCaptureMode::CaptureError, TestCaptureMode::CaptureException})
        {
            test_capture_mode = mode;
            check(camera.start() && waitForStopped(camera), "capture failure did not stop");
            check(!camera.stats().last_error.empty() && !frames.getLatest(), "capture failure diagnostics");
            // 不手动 stop，下次 start 必须回收上一个 joinable 线程。
        }
        test_capture_mode = TestCaptureMode::Frames;
        check(camera.start(), "restart after failure");
        std::cout << "CameraService lifecycle tests passed with test-only capture faults; no V4L2 hardware tested.\n";
        // 析构必须 join 正在运行的线程。
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
