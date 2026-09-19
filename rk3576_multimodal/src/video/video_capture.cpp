#include "video/video_capture.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace   //匿名命名空间  xioctl() 只给当前这个 .cpp 文件使用，不暴露给其他文件。
{

int xioctl(int fd, unsigned long request, void* arg)
{
    int ret;

    do
    {
        ret = ::ioctl(fd, request, arg);
    }
    while (ret == -1 && errno == EINTR);

    return ret;
}

} // namespace


VideoCapture::VideoCapture(const std::string& device,uint32_t width,uint32_t height,uint32_t pixel_format) 
    : device_(device),
      width_(width),          // 初始化列表
      height_(height),
      pixel_format_(pixel_format)
{
}


VideoCapture::~VideoCapture()
{
    shutdown();
}


bool VideoCapture::initialize()
{
    fd_ = ::open(device_.c_str(), O_RDWR);

    if (fd_ < 0)
    {
        std::cerr << "Failed to open "
                  << device_
                  << ": "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    if (!configureDevice())
    {
        shutdown();
        return false;
    }

    if (!initMMap())
    {
        shutdown();
        return false;
    }

    if (!startStreaming())
    {
        shutdown();
        return false;
    }

    return true;
}


bool VideoCapture::configureDevice()
{
    v4l2_capability capability{};

    if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0)
    {
        std::cerr << "VIDIOC_QUERYCAP failed"
                  << std::endl;
        return false;
    }

    if (!(capability.capabilities &V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        std::cerr
            << "Device does not support "
            << "V4L2 multi-planar capture"
            << std::endl;

        return false;
    }

    if (!(capability.capabilities &V4L2_CAP_STREAMING))
    {
        std::cerr
            << "Device does not support "
            << "V4L2 streaming"
            << std::endl;

        return false;
    }

    v4l2_format format{};

    format.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    format.fmt.pix_mp.width = width_;
    format.fmt.pix_mp.height = height_;
    format.fmt.pix_mp.pixelformat =pixel_format_;

    format.fmt.pix_mp.field =
        V4L2_FIELD_ANY;

    if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0)
    {
        std::cerr << "VIDIOC_S_FMT failed"
                  << std::endl;
        return false;
    }

    width_ = format.fmt.pix_mp.width;
    height_ = format.fmt.pix_mp.height;
    pixel_format_ =
        format.fmt.pix_mp.pixelformat;

    num_planes_ =
        format.fmt.pix_mp.num_planes;

    if (num_planes_ == 0)
    {
        std::cerr
            << "Driver returned zero planes"
            << std::endl;
        return false;
    }

    std::cout
        << "Camera format: "
        << width_
        << "x"
        << height_
        << ", planes="
        << num_planes_
        << std::endl;

    return true;
}


bool VideoCapture::initMMap()
{
    v4l2_requestbuffers request{};

    request.count = 4;

    request.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    request.memory = V4L2_MEMORY_MMAP;

    if (xioctl(
            fd_,
            VIDIOC_REQBUFS,
            &request) < 0)
    {
        std::cerr
            << "VIDIOC_REQBUFS failed"
            << std::endl;

        return false;
    }

    if (request.count < 2)
    {
        std::cerr
            << "Not enough V4L2 buffers"
            << std::endl;

        return false;
    }

    buffers_.resize(request.count);

    for (uint32_t i = 0;
         i < request.count;
         ++i)
    {
        std::vector<v4l2_plane>
            planes(num_planes_);

        v4l2_buffer buffer{};

        buffer.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buffer.memory =
            V4L2_MEMORY_MMAP;

        buffer.index = i;

        buffer.length = num_planes_;
        buffer.m.planes = planes.data();

        if (xioctl(
                fd_,
                VIDIOC_QUERYBUF,
                &buffer) < 0)
        {
            std::cerr
                << "VIDIOC_QUERYBUF failed"
                << std::endl;

            return false;
        }

        buffers_[i].planes.resize(
            num_planes_
        );

        for (uint32_t p = 0;
             p < num_planes_;
             ++p)
        {
            void* mapped =
                ::mmap(
                    nullptr,
                    planes[p].length,
                    PROT_READ |
                    PROT_WRITE,
                    MAP_SHARED,
                    fd_,
                    planes[p].m.mem_offset
                );

            if (mapped == MAP_FAILED)
            {
                std::cerr
                    << "mmap failed"
                    << std::endl;

                return false;
            }

            buffers_[i].planes[p].start =
                mapped;

            buffers_[i].planes[p].length =
                planes[p].length;
        }
    }

    return true;
}


bool VideoCapture::startStreaming()
{
    for (uint32_t i = 0;
         i < buffers_.size();
         ++i)
    {
        std::vector<v4l2_plane>
            planes(num_planes_);

        for (uint32_t p = 0;
             p < num_planes_;
             ++p)
        {
            planes[p].length =
                buffers_[i].planes[p].length;
        }

        v4l2_buffer buffer{};

        buffer.type =
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        buffer.memory =
            V4L2_MEMORY_MMAP;

        buffer.index = i;

        buffer.length = num_planes_;
        buffer.m.planes = planes.data();

        if (xioctl(
                fd_,
                VIDIOC_QBUF,
                &buffer) < 0)
        {
            std::cerr
                << "VIDIOC_QBUF failed"
                << std::endl;

            return false;
        }
    }

    v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (xioctl(
            fd_,
            VIDIOC_STREAMON,
            &type) < 0)
    {
        std::cerr
            << "VIDIOC_STREAMON failed"
            << std::endl;

        return false;
    }

    streaming_ = true;

    return true;
}


bool VideoCapture::captureFrame(
    ImageFrame& frame
)
{
    std::vector<v4l2_plane>
        planes(num_planes_);

    v4l2_buffer buffer{};

    buffer.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    buffer.memory =
        V4L2_MEMORY_MMAP;

    buffer.length = num_planes_;
    buffer.m.planes = planes.data();

    if (xioctl(
            fd_,
            VIDIOC_DQBUF,
            &buffer) < 0)
    {
        std::cerr
            << "VIDIOC_DQBUF failed: "
            << std::strerror(errno)
            << std::endl;

        return false;
    }

    if (buffer.index >= buffers_.size())
    {
        std::cerr
            << "Invalid buffer index"
            << std::endl;

        return false;
    }

    ImageFrame captured;

    captured.width = width_;
    captured.height = height_;
    captured.pixel_format =
        pixel_format_;

    captured.frame_id =
        ++frame_id_;

    captured.timestamp_ns =
        static_cast<uint64_t>(
            buffer.timestamp.tv_sec
        ) * 1000000000ULL
        +
        static_cast<uint64_t>(
            buffer.timestamp.tv_usec
        ) * 1000ULL;

    std::size_t total_size = 0;

    for (uint32_t p = 0;
         p < num_planes_;
         ++p)
    {
        if (planes[p].bytesused >
            planes[p].data_offset)
        {
            total_size +=
                planes[p].bytesused -
                planes[p].data_offset;
        }
    }

    captured.data.reserve(total_size);

    for (uint32_t p = 0;
         p < num_planes_;
         ++p)
    {
        std::size_t offset =
            planes[p].data_offset;

        std::size_t bytes =
            0;

        if (planes[p].bytesused > offset)
        {
            bytes =
                planes[p].bytesused -
                offset;
        }

        const uint8_t* start =
            static_cast<const uint8_t*>(
                buffers_[buffer.index]
                    .planes[p]
                    .start
            )
            + offset;

        captured.data.insert(
            captured.data.end(),
            start,
            start + bytes
        );
    }

    if (xioctl(
            fd_,
            VIDIOC_QBUF,
            &buffer) < 0)
    {
        std::cerr
            << "VIDIOC_QBUF failed"
            << std::endl;

        return false;
    }

    frame = std::move(captured);

    return true;
}


void VideoCapture::stopStreaming()
{
    if (!streaming_)
    {
        return;
    }

    v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    ::ioctl(
        fd_,
        VIDIOC_STREAMOFF,
        &type
    );

    streaming_ = false;
}


void VideoCapture::releaseMMap()
{
    for (auto& buffer : buffers_)
    {
        for (auto& plane : buffer.planes)
        {
            if (plane.start != nullptr)
            {
                ::munmap(
                    plane.start,
                    plane.length
                );

                plane.start = nullptr;
                plane.length = 0;
            }
        }
    }

    buffers_.clear();
}


void VideoCapture::shutdown()
{
    stopStreaming();

    releaseMMap();

    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}