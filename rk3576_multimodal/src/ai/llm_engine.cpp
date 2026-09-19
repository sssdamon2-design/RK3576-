#include "ai/llm_engine.h"
#include "common/timing.h"
#include "model_validation.h"
#include <exception>
#include <utility>

LLMEngine::LLMEngine(std::unique_ptr<ILLMBackend> backend) : backend_(std::move(backend)) {}
LLMEngine::~LLMEngine() { shutdown(); }

bool LLMEngine::initialize(const BackendConfig& config, std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_) backend_->shutdown();
    initialized_ = false;
    error.clear();
    try
    {
        if (!backend_) error = "LLM backend is null";
        else if (validateModelPaths(config, error))
            initialized_ = backend_->initialize(config, error);
    }
    catch (const std::exception& e) { error = std::string("LLM initialization exception: ") + e.what(); }
    catch (...) { error = "LLM initialization: unknown exception"; }
    if (!initialized_)
    {
        if (backend_) backend_->shutdown();
        if (error.empty()) error = "LLM model initialization failed without diagnostic";
    }
    else error.clear();
    initialization_error_ = error;
    return initialized_;
}
InferenceResult LLMEngine::infer(const std::string& question)
{
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    InferenceResult result;
    if (question.empty()) result.error_message = "LLM question is empty";
    else if (!initialized_)
        result.error_message = initialization_error_.empty()
            ? "LLM engine is not initialized" : initialization_error_;
    else
    {
        try { result = backend_->infer(question); }
        catch (const std::exception& e) { result.error_message = std::string("LLM inference exception: ") + e.what(); }
        catch (...) { result.error_message = "LLM inference: unknown exception"; }
        if (!result.success && result.error_message.empty())
            result.error_message = "LLM model inference failed without diagnostic";
    }
    if (!result.success) result.text.clear();
    result.latency_ms = elapsedMilliseconds(start);
    return result;
}
void LLMEngine::shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (backend_) backend_->shutdown();
    initialized_ = false;
    initialization_error_.clear();
}
