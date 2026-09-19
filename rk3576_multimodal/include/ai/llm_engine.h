#pragma once
#include <memory>
#include <mutex>
#include "ai/llm_backend.h"

class LLMEngine
{
public:
    explicit LLMEngine(std::unique_ptr<ILLMBackend> backend);
    ~LLMEngine();
    bool initialize(const BackendConfig& config, std::string& error);
    InferenceResult infer(const std::string& question);
    void shutdown() noexcept;
private:
    // 每个 runtime 实例串行访问；与 Camera 的锁完全独立。
    std::mutex mutex_;
    std::unique_ptr<ILLMBackend> backend_;
    bool initialized_ = false;
    std::string initialization_error_;
};
