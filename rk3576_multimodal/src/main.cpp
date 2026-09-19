#include <iostream>

#include <linux/videodev2.h>

#include "video/video_capture.h"

int main()
{
    VideoCapture camera(
        "/dev/video11",
        3840,
        2160,
        V4L2_PIX_FMT_NV12
    );

    if (!camera.initialize())
    {
        std::cerr
            << "Camera initialization failed"
            << std::endl;

        return 1;
    }

    ImageFrame frame;

    if (!camera.captureFrame(frame))
    {
        std::cerr
            << "Capture frame failed"
            << std::endl;

        return 1;
    }

    std::cout
        << "Capture success"
        << std::endl;

    std::cout
        << "Frame ID: "
        << frame.frame_id
        << std::endl;

    std::cout
        << "Resolution: "
        << frame.width
        << "x"
        << frame.height
        << std::endl;

    std::cout
        << "Bytes: "
        << frame.data.size()
        << std::endl;

    std::cout
        << "Timestamp(ns): "
        << frame.timestamp_ns
        << std::endl;

    camera.shutdown();

    return 0;
}