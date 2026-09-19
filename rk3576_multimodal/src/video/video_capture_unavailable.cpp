#include "video/video_capture.h"

// 仅非 Linux 构建选择此文件，不替代 Linux V4L2 实现，也不生成模拟帧。
VideoCapture::VideoCapture(const std::string& device, uint32_t width,
                           uint32_t height, uint32_t pixel_format)
    : device_(device), width_(width), height_(height), pixel_format_(pixel_format) {}
VideoCapture::~VideoCapture() { shutdown(); }
bool VideoCapture::initialize()
{
    last_error_ = "V4L2 camera unavailable on this platform; RK3576 Linux hardware validation required";
    return false;
}
bool VideoCapture::captureFrame(ImageFrame& frame)
{
    return captureFrame(frame, 200) == CaptureStatus::Frame;
}
VideoCapture::CaptureStatus VideoCapture::captureFrame(ImageFrame&, int)
{
    last_error_ = "V4L2 capture unavailable on this platform";
    return CaptureStatus::Error;
}
void VideoCapture::shutdown() {}
