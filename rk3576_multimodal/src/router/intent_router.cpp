#include "router/intent_router.h"
#include <algorithm>

// UTF-8 子串规则。只识别当前画面请求，避免把“摄像头原理”一概当作视觉问题。
RouteDecision IntentRouter::route(const std::string& question) const
{
    std::string normalized = question;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c); });
    const char* vision_phrases[] = {
        u8"前面有什么", u8"摄像头里", u8"面前是什么", u8"看看前方",
        u8"当前画面", u8"现在的画面", u8"摄像头看到", u8"你看到了什么",
        u8"眼前有什么", u8"看看这个", u8"描述画面",
        "what do you see", "what is in front", "what's in front",
        "camera view", "describe the scene", "current image"
    };
    for (const auto* phrase : vision_phrases)
        if (normalized.find(phrase) != std::string::npos)
            return {RequestType::Vision, std::string("matched: ") + phrase};
    return {RequestType::Text, "default text rule"};
}
const char* requestTypeName(RequestType type)
{
    return type == RequestType::Vision ? "VISION" : "TEXT";
}
