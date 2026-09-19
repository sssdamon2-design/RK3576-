#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "video/latest_frame_buffer.h"
#include "video/video_capture.h"

class CameraService
{
public:
    CameraService(
        const std::string& device,
        uint32_t width,
        uint32_t height,
        uint32_t pixel_format,
        LatestFrameBuffer& latest_frame_buffer
    );

    ~CameraService();

    bool start();

    void stop();

    bool isRunning() const;

private:
    void captureLoop();

private:
    VideoCapture camera_;

    LatestFrameBuffer& latest_frame_buffer_;

    std::thread capture_thread_;

    std::atomic<bool> running_{false};
};