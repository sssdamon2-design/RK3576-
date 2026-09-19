#include "video/latest_frame_buffer.h"

#include <utility>

void LatestFrameBuffer::update(FramePtr frame)
{
    std::lock_guard<std::mutex> lock(mutex_);   //自动加锁工具。 mutex锁上  离开函数自动解锁
 
    latest_frame_ = std::move(frame);   //把frame 这个局部变量我后面不用了，可以把它持有的资源直接交给 latest_frame_。
}

LatestFrameBuffer::FramePtr LatestFrameBuffer::getLatest() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return latest_frame_;
}