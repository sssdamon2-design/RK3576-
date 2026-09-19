#pragma once
#include "ai/backend_config.h"
#include "common/inference_result.h"
#include "video/latest_frame_buffer.h"

class IVLMBackend
{
public:
    virtual ~IVLMBackend() = default;
    virtual bool initialize(const BackendConfig& config, std::string& error) = 0;
    // 同步接口：返回前完成推理。shared_ptr 保障整段推理期间帧有效。
    virtual InferenceResult infer(LatestFrameBuffer::FramePtr frame,
                                  const std::string& question) = 0;
    virtual void shutdown() noexcept = 0;
};
