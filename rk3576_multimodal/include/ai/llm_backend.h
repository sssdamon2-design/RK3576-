#pragma once
#include "ai/backend_config.h"
#include "common/inference_result.h"

class ILLMBackend
{
public:
    virtual ~ILLMBackend() = default;
    virtual bool initialize(const BackendConfig& config, std::string& error) = 0;
    virtual InferenceResult infer(const std::string& question) = 0;
    // 必须可重复调用，并释放部分初始化留下的资源。
    virtual void shutdown() noexcept = 0;
};
