#include "app/request_processor.h"
#include <utility>

RequestProcessor::RequestProcessor(LatestFrameBuffer& frames, LLMEngine& llm, VLMEngine& vlm)
    : frames_(frames), llm_(llm), vlm_(vlm) {}
RequestResult RequestProcessor::process(const std::string& question)
{
    RequestResult result;
    result.route = router_.route(question);
    if (result.route.type == RequestType::Vision)
    {
        // getLatest 返回时锁已释放。整个 VLM 推理都不持有帧仓库的 mutex。
        auto frame = frames_.getLatest();
        result.inference = vlm_.infer(std::move(frame), question);
    }
    else result.inference = llm_.infer(question);
    return result;
}
