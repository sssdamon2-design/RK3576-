#include "video/latest_frame_buffer.h"

void LatestFrameBuffer::update(FramePtr frame)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_frame_.swap(frame);
    }
    // 旧帧可能很大，在锁外释放内存。
}
LatestFrameBuffer::FramePtr LatestFrameBuffer::getLatest() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_frame_;
}
void LatestFrameBuffer::clear() { update(nullptr); }
