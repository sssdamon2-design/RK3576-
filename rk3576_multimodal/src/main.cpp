#include <exception>
#include <iostream>
#include <memory>
#include "ai/rk_backends.h"
#include "app/cli.h"

int main(int argc, char** argv)
{
    try
    {
        AppConfig config;
        std::string error;
        if (!parseArguments(argc, argv, config, error))
        {
            std::cerr << error << '\n';
            printUsage();
            return 2;
        }
        if (config.help) { printUsage(); return 0; }

        LatestFrameBuffer frames; // 先构造，后析构；保证服务持有的引用有效。
        // NV12 FourCC 是格式标识，不依赖 Linux 头文件；V4L2 实现在 video 模块。
        constexpr uint32_t nv12 = uint32_t('N') | (uint32_t('V') << 8) |
                                  (uint32_t('1') << 16) | (uint32_t('2') << 24);
        CameraService camera(config.camera_device, 3840, 2160, nv12, frames);
        if (config.use_camera && !camera.start())
            std::cerr << camera.stats().last_error << '\n';
        if (!config.use_camera) std::cout << "Camera disabled by --no-camera.\n";

        LLMEngine llm(std::make_unique<RKLLMBackend>());
        VLMEngine vlm(std::make_unique<RK3576VLMBackend>());
        if (!llm.initialize(config.llm, error)) std::cerr << "LLM initialization: " << error << '\n';
        if (!vlm.initialize(config.vlm, error)) std::cerr << "VLM initialization: " << error << '\n';

        RequestProcessor processor(frames, llm, vlm);
        const int status = runCli(processor, camera, frames);
        camera.stop();
        llm.shutdown();
        vlm.shutdown();
        return status;
        // 异常路径同样通过各模块析构函数释放资源。
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal application exception: " << e.what() << '\n';
    }
    catch (...) { std::cerr << "Fatal application exception: unknown exception\n"; }
    return 1;
}
