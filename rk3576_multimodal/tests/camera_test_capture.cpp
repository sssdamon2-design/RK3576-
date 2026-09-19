#include "camera_test_capture.h"
#include "video/video_capture.h"
#include <chrono>
#include <stdexcept>
#include <thread>

std::atomic<TestCaptureMode> test_capture_mode{TestCaptureMode::Frames};
std::atomic<int> test_capture_calls{0};
VideoCapture::VideoCapture(const std::string& device, uint32_t width, uint32_t height, uint32_t format)
    : device_(device), width_(width), height_(height), pixel_format_(format) {}
VideoCapture::~VideoCapture() { shutdown(); }
bool VideoCapture::initialize()
{
    if (test_capture_mode.load() == TestCaptureMode::InitException)
        throw std::runtime_error("test init exception");
    if (test_capture_mode.load() == TestCaptureMode::InitFailure)
    {
        last_error_ = "test camera open failure";
        return false;
    }
    return true;
}
bool VideoCapture::captureFrame(ImageFrame& frame)
{
    return captureFrame(frame, 200) == CaptureStatus::Frame;
}
VideoCapture::CaptureStatus VideoCapture::captureFrame(ImageFrame& frame, int timeout_ms)
{
    ++test_capture_calls;
    const auto mode = test_capture_mode.load();
    if (mode == TestCaptureMode::Timeout)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return CaptureStatus::Timeout;
    }
    if (mode == TestCaptureMode::CaptureException) throw std::runtime_error("test capture exception");
    if (mode == TestCaptureMode::CaptureError)
    {
        last_error_ = "test DQBUF failure";
        return CaptureStatus::Error;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    frame.frame_id = ++frame_id_;
    frame.width = 2;
    frame.height = 2;
    frame.data = {1, 2, 3, 4, 5, 6};
    frame.received_at = std::chrono::steady_clock::now();
    return CaptureStatus::Frame;
}
void VideoCapture::shutdown() {}
