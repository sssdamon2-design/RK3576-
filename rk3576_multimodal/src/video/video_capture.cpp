#include "video/video_capture.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
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
    shutdown();
    last_error_.clear();
    fd_ = ::open(device_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);

    if (fd_ < 0)
    {
        return fail("open " + device_);
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
        return fail("VIDIOC_QUERYCAP");
    }

    const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? capability.device_caps : capability.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        return fail("Device does not support V4L2 multi-planar capture", false);
    }

    if (!(caps & V4L2_CAP_STREAMING))
    {
        return fail("Device does not support V4L2 streaming", false);
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
        return fail("VIDIOC_S_FMT");
    }

    width_ = format.fmt.pix_mp.width;
    height_ = format.fmt.pix_mp.height;
    pixel_format_ =
        format.fmt.pix_mp.pixelformat;

    num_planes_ =
        format.fmt.pix_mp.num_planes;

    if (num_planes_ == 0 || num_planes_ > VIDEO_MAX_PLANES)
    {
        return fail("Driver returned invalid plane count", false);
    }

    plane_strides_.clear();
    for (uint32_t p = 0; p < num_planes_; ++p)
        plane_strides_.push_back(format.fmt.pix_mp.plane_fmt[p].bytesperline);

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
        return fail("VIDIOC_REQBUFS");
    }

    if (request.count < 2)
    {
        return fail("Not enough V4L2 buffers", false);
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
            return fail("VIDIOC_QUERYBUF buffer=" + std::to_string(i));
        }

        if (buffer.length != num_planes_)
            return fail("VIDIOC_QUERYBUF plane count mismatch", false);

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
                return fail("mmap buffer=" + std::to_string(i) + " plane=" + std::to_string(p));
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
            return fail("VIDIOC_QBUF buffer=" + std::to_string(i));
        }
    }

    v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (xioctl(
            fd_,
            VIDIOC_STREAMON,
            &type) < 0)
    {
        return fail("VIDIOC_STREAMON");
    }

    streaming_ = true;

    return true;
}


bool VideoCapture::captureFrame(
    ImageFrame& frame
)
{
    const auto status = captureFrame(frame, 200);
    if (status == CaptureStatus::Timeout) fail("captureFrame timed out or was interrupted", false);
    return status == CaptureStatus::Frame;
}

VideoCapture::CaptureStatus VideoCapture::captureFrame(ImageFrame& frame, int timeout_ms)
{
    if (!streaming_ || fd_ < 0 || timeout_ms < 0)
    {
        fail("captureFrame requires initialized stream and nonnegative timeout", false);
        return CaptureStatus::Error;
    }
    pollfd descriptor{};
    descriptor.fd = fd_;
    descriptor.events = POLLIN;
    const int ready = ::poll(&descriptor, 1, timeout_ms);
    if (ready == 0 || (ready < 0 && errno == EINTR)) return CaptureStatus::Timeout;
    if (ready < 0)
    {
        fail("poll");
        return CaptureStatus::Error;
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        fail("poll device error revents=" + std::to_string(descriptor.revents), false);
        return CaptureStatus::Error;
    }
    if (!(descriptor.revents & POLLIN)) return CaptureStatus::Timeout;

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
        // poll 后状态仍可能变化，O_NONBLOCK 保证 DQBUF 不会无限等待。
        if (errno == EAGAIN) return CaptureStatus::Timeout;
        fail("VIDIOC_DQBUF");
        return CaptureStatus::Error;
    }

    if (buffer.index >= buffers_.size())
    {
        fail("VIDIOC_DQBUF invalid buffer index", false);
        return CaptureStatus::Error;
    }

    if (buffer.length != num_planes_ || (buffer.flags & V4L2_BUF_FLAG_ERROR))
    {
        fail("VIDIOC_DQBUF invalid planes or V4L2_BUF_FLAG_ERROR", false);
        return CaptureStatus::Error; // CameraService 随后 STREAMOFF，回收所有缓冲。
    }

    ImageFrame captured;
    captured.received_at = std::chrono::steady_clock::now();
    captured.plane_strides = plane_strides_;

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
        if (planes[p].data_offset > planes[p].bytesused ||
            planes[p].bytesused > buffers_[buffer.index].planes[p].length)
        {
            fail("VIDIOC_DQBUF plane bounds invalid, plane=" + std::to_string(p), false);
            return CaptureStatus::Error;
        }
        captured.plane_sizes.push_back(planes[p].bytesused - planes[p].data_offset);
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

        if (bytes == 0) continue;
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
        fail("VIDIOC_QBUF after capture");
        return CaptureStatus::Error;
    }

    frame = std::move(captured);

    return CaptureStatus::Frame;
}


void VideoCapture::stopStreaming()
{
    if (!streaming_)
    {
        return;
    }

    v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (::ioctl(
        fd_,
        VIDIOC_STREAMOFF,
        &type
    ) < 0)
        std::cerr << "VIDIOC_STREAMOFF: " << std::strerror(errno) << std::endl;

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
                if (::munmap(
                    plane.start,
                    plane.length
                ) < 0)
                    std::cerr << "munmap: " << std::strerror(errno) << std::endl;

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
        if (::close(fd_) < 0)
            std::cerr << "close camera: " << std::strerror(errno) << std::endl;
        fd_ = -1;
    }
}

bool VideoCapture::fail(const std::string& operation, bool include_errno)
{
    last_error_ = device_ + ": " + operation;
    if (include_errno) last_error_ += ": " + std::string(std::strerror(errno));
    return false;
}
