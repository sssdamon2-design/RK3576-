#pragma once
#include <memory>
#include <mutex>
#include "common/types.h"

class LatestFrameBuffer
{
public:
    // 消费者共享帧的所有权；更新最新帧不会销毁推理正在使用的旧帧。
    using FramePtr = std::shared_ptr<const ImageFrame>;
    void update(FramePtr frame);
    FramePtr getLatest() const;
    void clear();
private:
    mutable std::mutex mutex_;
    FramePtr latest_frame_;
};
