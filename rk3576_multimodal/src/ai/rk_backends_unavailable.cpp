#include "ai/rk_backends.h"

namespace
{
const char* llm_error =
    "RK3576 Runtime/SDK not available: RKLLM headers, library and verified adapter are absent";
const char* vlm_error =
    "RK3576 Runtime/SDK not available: VLM runtime, vision encoder and verified adapter are absent";
InferenceResult unavailable(const char* message)
{
    InferenceResult result;
    result.error_message = message;
    return result;
}
}
bool RKLLMBackend::initialize(const BackendConfig&, std::string& error)
{
    error = llm_error;
    return false;
}
InferenceResult RKLLMBackend::infer(const std::string&) { return unavailable(llm_error); }
void RKLLMBackend::shutdown() noexcept {}
bool RK3576VLMBackend::initialize(const BackendConfig&, std::string& error)
{
    error = vlm_error;
    return false;
}
InferenceResult RK3576VLMBackend::infer(LatestFrameBuffer::FramePtr, const std::string&)
{
    return unavailable(vlm_error);
}
void RK3576VLMBackend::shutdown() noexcept {}
