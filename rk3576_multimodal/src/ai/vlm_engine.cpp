#include "ai/vlm_engine.h"
#include "common/timing.h"
#include "model_validation.h"
#include <exception>
#include <utility>

VLMEngine::VLMEngine(std::unique_ptr<IVLMBackend> backend) : backend_(std::move(backend)) {}
VLMEngine::~VLMEngine() { shutdown(); }

bool VLMEngine::initialize(const BackendConfig& config, std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_) backend_->shutdown();
    initialized_ = false;
    error.clear();
    try
    {
        if (!backend_) error = "VLM backend is null";
        else if (validateModelPaths(config, error))
            initialized_ = backend_->initialize(config, error);
    }
    catch (const std::exception& e) { error = std::string("VLM initialization exception: ") + e.what(); }
    catch (...) { error = "VLM initialization: unknown exception"; }
    if (!initialized_)
    {
        if (backend_) backend_->shutdown();
        if (error.empty()) error = "VLM model initialization failed without diagnostic";
    }
    else error.clear();
    initialization_error_ = error;
    return initialized_;
}
InferenceResult VLMEngine::infer(LatestFrameBuffer::FramePtr frame, const std::string& question)
{
    const auto start = std::chrono::steady_clock::now();
    // 在请求入口记录帧龄；包括取帧后的排队时间时可另加 runtime 指标。
    const double age = frame ? frameAgeMilliseconds(*frame) : -1.0;
    std::lock_guard<std::mutex> lock(mutex_);
    InferenceResult result;
    if (!frame) result.error_message = "No latest camera frame available";
    else if (frame->data.empty() || frame->width == 0 || frame->height == 0)
        result.error_message = "Camera frame is empty or has invalid dimensions";
    else if (question.empty()) result.error_message = "VLM question is empty";
    else if (!initialized_)
        result.error_message = initialization_error_.empty()
            ? "VLM engine is not initialized" : initialization_error_;
    else
    {
        try { result = backend_->infer(frame, question); }
        catch (const std::exception& e) { result.error_message = std::string("VLM inference exception: ") + e.what(); }
        catch (...) { result.error_message = "VLM inference: unknown exception"; }
        if (!result.success && result.error_message.empty())
            result.error_message = "VLM model inference failed without diagnostic";
    }
    if (frame)
    {
        result.frame_id = frame->frame_id;
        result.frame_timestamp_ns = frame->timestamp_ns;
        result.frame_age_ms = age;
    }
    if (!result.success) result.text.clear();
    result.latency_ms = elapsedMilliseconds(start);
    return result;
}
void VLMEngine::shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_) backend_->shutdown();
    initialized_ = false;
    initialization_error_.clear();
}
