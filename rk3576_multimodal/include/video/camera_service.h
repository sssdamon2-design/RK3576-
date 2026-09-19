#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include "video/latest_frame_buffer.h"
#include "video/video_capture.h"

struct CameraStats
{
    uint64_t frame_count = 0;
    uint64_t timeout_count = 0;
    double approximate_fps = 0.0;
    bool running = false;
    std::string last_error;
};

class CameraService
{
public:
    CameraService(const std::string& device, uint32_t width, uint32_t height,
                  uint32_t pixel_format, LatestFrameBuffer& latest_frame_buffer);
    ~CameraService();
    CameraService(const CameraService&) = delete;
    CameraService& operator=(const CameraService&) = delete;
    bool start();
    void stop();
    bool isRunning() const;
    CameraStats stats() const;
private:
    void captureLoop();
    void setError(const std::string& message);
    VideoCapture camera_;
    LatestFrameBuffer& latest_frame_buffer_; // 必须比本服务活得更久。
    std::thread capture_thread_;
    std::atomic<bool> running_{false}; // 主线程请求停止，采集线程读取。
    std::mutex lifecycle_mutex_; // 串行化 start/stop；采集线程不持有它。
    mutable std::mutex stats_mutex_;
    CameraStats stats_;
    std::chrono::steady_clock::time_point started_at_{};
    std::chrono::steady_clock::time_point stopped_at_{};
};
