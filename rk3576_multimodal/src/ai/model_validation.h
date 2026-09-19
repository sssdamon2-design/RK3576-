#pragma once
#include <filesystem>
#include "ai/backend_config.h"

// 只检查传入的路径；是否必填、模型格式和兼容性由真实 backend 决定。
inline bool validateModelPaths(const BackendConfig& config, std::string& error)
{
    for (const auto& path : {config.model_path, config.vision_model_path})
    {
        if (path.empty()) continue;
        std::error_code code;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), code))
        {
            error = "Model file does not exist or is not a regular file: " + path;
            if (code) error += " (" + code.message() + ")";
            return false;
        }
    }
    return true;
}
