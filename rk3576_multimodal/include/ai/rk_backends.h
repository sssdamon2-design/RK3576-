#pragma once
#include "ai/llm_backend.h"
#include "ai/vlm_backend.h"

// Runtime 适配的唯一入口。目前仓库没有 SDK，明确失败，不产生模拟回答。
class RKLLMBackend final : public ILLMBackend
{
public:
    bool initialize(const BackendConfig& config, std::string& error) override;
    InferenceResult infer(const std::string& question) override;
    void shutdown() noexcept override;
};
class RK3576VLMBackend final : public IVLMBackend
{
public:
    bool initialize(const BackendConfig& config, std::string& error) override;
    InferenceResult infer(LatestFrameBuffer::FramePtr frame,
                          const std::string& question) override;
    void shutdown() noexcept override;
};
