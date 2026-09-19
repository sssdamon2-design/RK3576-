#pragma once
#include <string>

// 路径由 CLI 传入；不在源码中嵌入模型或 SDK。
struct BackendConfig
{
    std::string model_path;
    std::string vision_model_path; // VLM 可能使用独立视觉编码器，具体格式由 SDK 决定。
};
