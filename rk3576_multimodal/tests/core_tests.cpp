#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include "ai/rk_backends.h"
#include "app/request_processor.h"
#include "common/timing.h"

namespace
{
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
class RejectingLLM final : public ILLMBackend
{
public:
    int calls = 0;
    int shutdowns = 0;
    bool throw_init = false;
    bool throw_infer = false;
    bool initialize(const BackendConfig&, std::string&) override
    {
        if (throw_init) throw std::runtime_error("test initialization fault");
        return true;
    }
    InferenceResult infer(const std::string&) override
    {
        ++calls;
        if (throw_infer) throw std::runtime_error("test inference fault");
        InferenceResult result;
        result.error_message = "test-only backend rejection";
        return result;
    }
    void shutdown() noexcept override { ++shutdowns; }
};
class RejectingVLM final : public IVLMBackend
{
public:
    int calls = 0;
    bool throw_infer = false;
    std::atomic<bool> entered{false};
    std::shared_future<void> release;
    bool initialize(const BackendConfig&, std::string&) override { return true; }
    InferenceResult infer(LatestFrameBuffer::FramePtr frame, const std::string&) override
    {
        ++calls;
        entered.store(true);
        if (release.valid()) release.wait_for(std::chrono::seconds(3));
        if (throw_infer) throw std::runtime_error("test vision fault");
        check(frame && frame->frame_id == 7, "held frame changed during inference");
        InferenceResult result;
        result.error_message = "test-only vision rejection";
        return result;
    }
    void shutdown() noexcept override {}
};
std::shared_ptr<const ImageFrame> makeFrame(uint64_t id)
{
    ImageFrame frame;
    frame.frame_id = id;
    frame.timestamp_ns = 123456;
    frame.width = 2;
    frame.height = 2;
    frame.data = {1, 2, 3, 4, 5, 6};
    frame.received_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(10);
    return std::make_shared<const ImageFrame>(std::move(frame));
}
void testRouter()
{
    IntentRouter router;
    for (const auto* question : {u8"你是谁", u8"介绍一下 Transformer", u8"Linux mutex 是什么",
                                u8"摄像头的工作原理是什么"})
        check(router.route(question).type == RequestType::Text, "text route");
    for (const auto* question : {u8"前面有什么", u8"摄像头里有什么", u8"现在我面前是什么",
                                u8"帮我看看前方", u8"当前画面中有什么", "WHAT DO YOU SEE?"})
        check(router.route(question).type == RequestType::Vision, "vision route");
}
void testUnavailable()
{
    LLMEngine llm(std::make_unique<RKLLMBackend>());
    VLMEngine vlm(std::make_unique<RK3576VLMBackend>());
    std::string error;
    check(!llm.initialize({}, error), "unavailable LLM initialized");
    check(error.find("RK3576 Runtime/SDK not available") != std::string::npos, "missing runtime message");
    auto result = llm.infer("hello");
    check(!result.success && result.text.empty() && result.latency_ms >= 0, "unavailable fabricated answer");
    check(!vlm.initialize({}, error), "unavailable VLM initialized");
    result = vlm.infer(nullptr, "scene");
    check(!result.success && result.error_message.find("No latest") != std::string::npos, "no frame error");
    result = vlm.infer(makeFrame(7), "scene");
    check(!result.success && result.frame_id == 7 && result.frame_timestamp_ns == 123456, "frame metadata");
    check(result.frame_age_ms >= 0 && result.error_message.find("Runtime/SDK") != std::string::npos, "VLM unavailable");
    BackendConfig config;
    config.model_path = "__nonexistent_model_for_test__/missing.rkllm";
    check(!llm.initialize(config, error) && error.find("Model file") != std::string::npos, "missing model path");
    config.model_path.clear();
    config.vision_model_path = "__nonexistent_model_for_test__/vision.rknn";
    check(!vlm.initialize(config, error) && error.find("Model file") != std::string::npos, "missing vision model");
    llm.shutdown(); llm.shutdown();
    vlm.shutdown(); vlm.shutdown();
    LLMEngine null_llm(nullptr);
    VLMEngine null_vlm(nullptr);
    check(!null_llm.initialize({}, error), "null LLM");
    check(!null_vlm.initialize({}, error), "null VLM");
}
void testExceptionsAndRouting()
{
    LatestFrameBuffer frames;
    auto llm_backend = std::make_unique<RejectingLLM>();
    auto* llm_state = llm_backend.get();
    auto vlm_backend = std::make_unique<RejectingVLM>();
    auto* vlm_state = vlm_backend.get();
    LLMEngine llm(std::move(llm_backend));
    VLMEngine vlm(std::move(vlm_backend));
    std::string error;
    llm_state->throw_init = true;
    check(!llm.initialize({}, error) && error.find("test initialization fault") != std::string::npos, "init exception");
    check(llm_state->shutdowns >= 2, "partial initialization cleanup");
    llm_state->throw_init = false;
    check(llm.initialize({}, error) && vlm.initialize({}, error), "test backend initialization");
    RequestProcessor processor(frames, llm, vlm);
    processor.process(u8"你是谁");
    check(llm_state->calls == 1 && vlm_state->calls == 0, "text dispatched incorrectly");
    processor.process(u8"前面有什么");
    check(vlm_state->calls == 0, "called VLM with no frame");
    frames.update(makeFrame(7));
    auto response = processor.process(u8"前面有什么");
    check(vlm_state->calls == 1 && llm_state->calls == 1, "vision dispatched incorrectly");
    check(!response.inference.success && !response.inference.error_message.empty(), "inference rejection lost");
    llm_state->throw_infer = true;
    auto result = llm.infer("test");
    check(!result.success && result.error_message.find("test inference fault") != std::string::npos, "LLM exception");
    vlm_state->throw_infer = true;
    result = vlm.infer(frames.getLatest(), "test");
    check(!result.success && result.frame_id == 7 && result.error_message.find("test vision fault") != std::string::npos,
          "VLM exception metadata");
    result = vlm.infer(std::make_shared<const ImageFrame>(), "test");
    check(!result.success && result.error_message.find("empty") != std::string::npos, "empty frame");
    check(!llm.infer("").success, "empty question");
    llm.shutdown();
    check(!llm.infer("after shutdown").success, "infer after shutdown");
}
void testSlowConsumer()
{
    LatestFrameBuffer frames;
    frames.update(makeFrame(7));
    std::weak_ptr<const ImageFrame> old_frame = frames.getLatest();
    auto backend = std::make_unique<RejectingVLM>();
    auto* state = backend.get();
    std::promise<void> release;
    state->release = release.get_future().share();
    VLMEngine vlm(std::move(backend));
    LLMEngine llm(std::make_unique<RejectingLLM>());
    std::string error;
    check(vlm.initialize({}, error) && llm.initialize({}, error), "test init");
    RequestProcessor processor(frames, llm, vlm);
    auto consumer = std::async(std::launch::async, [&processor] { return processor.process(u8"前面有什么"); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!state->entered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool entered = state->entered.load();
    auto producer = std::async(std::launch::async, [&frames] {
        for (uint64_t id = 8; id <= 2000; ++id) frames.update(makeFrame(id));
    });
    const bool producer_finished = producer.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    const bool old_alive = !old_frame.expired();
    release.set_value(); // 即使检查失败，也先让工作线程退出。
    producer.get();
    const auto response = consumer.get();
    check(entered && producer_finished, "slow consumer blocked frame producer");
    check(old_alive && old_frame.expired(), "shared frame lifetime");
    check(response.inference.frame_id == 7 && frames.getLatest()->frame_id == 2000, "latest/held frame mismatch");
    frames.clear();
    check(!frames.getLatest(), "clear latest");
}
}
int main()
{
    try
    {
        testRouter();
        testUnavailable();
        testExceptionsAndRouting();
        testSlowConsumer();
        std::cout << "Portable core tests passed. Test doubles never return AI answers.\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
