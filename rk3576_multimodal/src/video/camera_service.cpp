#include "video/camera_service.h"

#include <iostream>
#include <memory>
#include <utility>


CameraService::CameraService(
    const std::string& device,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    LatestFrameBuffer& latest_frame_buffer
)
    : camera_(
          device,
          width,
          height,
          pixel_format
      ),
      latest_frame_buffer_(
          latest_frame_buffer
      )
{
}


CameraService::~CameraService()
{
    stop();
}


bool CameraService::start()
{
    if (running_.load())
    {
        return true;
    }

    if (!camera_.initialize())
    {
        std::cerr
            << "Camera initialization failed"
            << std::endl;

        return false;
    }

    running_.store(true);

    capture_thread_ =
        std::thread(
            &CameraService::captureLoop,
            this
        );

    return true;
}


void CameraService::stop()
{
    running_.store(false);

    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    camera_.shutdown();
}


bool CameraService::isRunning() const
{
    return running_.load();
}


void CameraService::captureLoop()
{
    while (running_.load())
    {
        ImageFrame frame;

        if (!camera_.captureFrame(frame))
        {
            std::cerr
                << "Camera capture failed"
                << std::endl;

            running_.store(false);

            break;
        }

        auto frame_ptr =
            std::make_shared<ImageFrame>(
                std::move(frame)
            );

        latest_frame_buffer_.update(
            frame_ptr
        );
    }
}