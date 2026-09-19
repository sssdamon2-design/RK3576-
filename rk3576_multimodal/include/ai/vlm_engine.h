#pragma once
#include <memory>
#include <mutex>
#include "ai/vlm_backend.h"

class VLMEngine
{
public:
    explicit VLMEngine(std::unique_ptr<IVLMBackend> backend);
    ~VLMEngine();
    bool initialize(const BackendConfig& config, std::string& error);
    InferenceResult infer(LatestFrameBuffer::FramePtr frame, const std::string& question);
    void shutdown() noexcept;
private:
    std::mutex mutex_;
    std::unique_ptr<IVLMBackend> backend_;
    bool initialized_ = false;
    std::string initialization_error_;
};
