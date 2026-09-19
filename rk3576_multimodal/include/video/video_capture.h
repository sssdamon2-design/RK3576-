#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

class VideoCapture
{
public:
    enum class CaptureStatus { Frame, Timeout, Error };
    VideoCapture(                               //构造函数  名字和类名相同，没有返回值。
        const std::string& device,            // & 表示引用 不复制整个字符串，直接引用调用者的字符串，而且不允许修改它。
        uint32_t width,
        uint32_t height,
        uint32_t pixel_format
    );

    ~VideoCapture();   //析构函数  VideoCapture 对象销毁的时候自动执行。

    bool initialize();

    VideoCapture(const VideoCapture&) = delete;
    VideoCapture& operator=(const VideoCapture&) = delete;
    // Timeout 可重试；Error 为致命采集错误，调用者应 shutdown。
    CaptureStatus captureFrame(ImageFrame& frame, int timeout_ms);
    const std::string& lastError() const { return last_error_; }

    bool captureFrame(ImageFrame& frame); //& frame  类似指针，但是这里叫做引用 即可以更改 frame本身  而不是复制一个形参

    void shutdown();

private:
    struct MMapPlane
    {
        void* start = nullptr;
        std::size_t length = 0;
    };

    struct MMapBuffer
    {
        std::vector<MMapPlane> planes;
    };

    bool configureDevice();
    bool initMMap();
    bool startStreaming();

    void stopStreaming();
    void releaseMMap();
    bool fail(const std::string& operation, bool include_errno = true);

private:
    std::string device_;

    uint32_t width_;
    uint32_t height_;
    uint32_t pixel_format_;

    int fd_ = -1;

    uint32_t num_planes_ = 0;

    std::vector<MMapBuffer> buffers_;

    bool streaming_ = false;

    uint64_t frame_id_ = 0;
    std::vector<uint32_t> plane_strides_;
    std::string last_error_;
};
