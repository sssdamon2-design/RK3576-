#pragma once
#include <string>
#include "ai/backend_config.h"
#include "app/request_processor.h"
#include "video/camera_service.h"

struct AppConfig
{
    std::string camera_device = "/dev/video11";
    bool use_camera = true;
    bool help = false;
    BackendConfig llm;
    BackendConfig vlm;
};
bool parseArguments(int argc, char** argv, AppConfig& config, std::string& error);
void printUsage();
int runCli(RequestProcessor& processor, CameraService& camera, LatestFrameBuffer& frames);
