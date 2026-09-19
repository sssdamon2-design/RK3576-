#include "app/cli.h"
#include "common/timing.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#else
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#endif

namespace
{
#ifdef __linux__
volatile std::sig_atomic_t stop_requested = 0;
void onSignal(int) { stop_requested = 1; }

class SignalGuard
{
public:
    SignalGuard()
    {
        stop_requested = 0;
        old_int_ = std::signal(SIGINT, onSignal);
        old_term_ = std::signal(SIGTERM, onSignal);
    }
    ~SignalGuard()
    {
        std::signal(SIGINT, old_int_);
        std::signal(SIGTERM, old_term_);
    }
private:
    using Handler = void (*)(int);
    Handler old_int_;
    Handler old_term_;
};
#endif

bool readLine(std::string& line)
{
#ifdef __linux__
    line.clear();
    // 不在 signal handler 中操作线程、锁或 C++ 对象。
    while (!stop_requested)
    {
        pollfd input{};
        input.fd = STDIN_FILENO;
        input.events = POLLIN;
        const int status = ::poll(&input, 1, 200);
        if (status < 0)
        {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("stdin poll: ") + std::strerror(errno));
        }
        if (status == 0) continue;
        if (input.revents & (POLLERR | POLLNVAL))
            throw std::runtime_error("stdin poll reported an invalid/error descriptor");
        char c = 0;
        const auto count = ::read(STDIN_FILENO, &c, 1);
        if (count == 0) return !line.empty();
        if (count < 0)
        {
            if (errno == EINTR || errno == EAGAIN) continue;
            throw std::runtime_error(std::string("stdin read: ") + std::strerror(errno));
        }
        if (c == '\n') return true;
        line.push_back(c);
    }
    return false;
#else
    return static_cast<bool>(std::getline(std::cin, line));
#endif
}
std::string trim(const std::string& text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
void showCameraError(CameraService& camera, std::string& previous)
{
    const auto state = camera.stats();
    if (!state.last_error.empty() && state.last_error != previous)
    {
        std::cerr << "Camera Error> " << state.last_error << '\n';
        previous = state.last_error;
    }
}
void showStats(CameraService& camera, LatestFrameBuffer& frames)
{
    const auto state = camera.stats();
    std::cout << "Camera: " << (state.running ? "running" : "stopped")
              << ", frame_count=" << state.frame_count
              << ", approximate_fps=" << state.approximate_fps
              << ", timeout_count=" << state.timeout_count << '\n';
    const auto frame = frames.getLatest();
    if (frame)
        std::cout << "Latest Frame ID: " << frame->frame_id
                  << ", Frame Age: " << frameAgeMilliseconds(*frame) << " ms\n";
    else std::cout << "Latest Frame: unavailable\n";
    if (!state.last_error.empty()) std::cout << "Camera last error: " << state.last_error << '\n';
}
}

bool parseArguments(int argc, char** argv, AppConfig& config, std::string& error)
{
    error.clear();
    for (int i = 1; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") config.help = true;
        else if (option == "--no-camera") config.use_camera = false;
        else if (option == "--device" || option == "--llm-model" ||
                 option == "--vlm-model" || option == "--vision-model")
        {
            if (++i >= argc || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
            {
                error = "Missing value for " + option;
                return false;
            }
            if (option == "--device") config.camera_device = argv[i];
            else if (option == "--llm-model") config.llm.model_path = argv[i];
            else if (option == "--vlm-model") config.vlm.model_path = argv[i];
            else config.vlm.vision_model_path = argv[i];
        }
        else
        {
            error = "Unknown argument: " + option;
            return false;
        }
    }
    return true;
}
void printUsage()
{
    std::cout << "rk3576_multimodal [--device /dev/video11] [--no-camera]\n"
              << "  [--llm-model PATH] [--vlm-model PATH] [--vision-model PATH]\n"
              << "CLI: enter a question; stats; exit; quit. Input encoding: UTF-8.\n"
              << "Models/SDK are external. No real inference backend is included yet.\n";
}
int runCli(RequestProcessor& processor, CameraService& camera, LatestFrameBuffer& frames)
{
#ifdef __linux__
    SignalGuard signals;
#endif
#ifdef _WIN32
    // 只设置控制台编码，不改变重定向文件的 UTF-8 数据。
    const UINT input_cp = GetConsoleCP();
    const UINT output_cp = GetConsoleOutputCP();
    struct ConsoleEncodingGuard
    {
        UINT input;
        UINT output;
        ~ConsoleEncodingGuard()
        {
            if (input) SetConsoleCP(input);
            if (output) SetConsoleOutputCP(output);
        }
    } encoding{input_cp, output_cp};
    if (input_cp) SetConsoleCP(CP_UTF8);
    if (output_cp) SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << std::fixed << std::setprecision(2)
              << "Commands: stats, exit, quit. Camera/runtime hardware requires RK3576 validation.\n";
    std::string question;
    std::string previous_camera_error;
    for (;;)
    {
        showCameraError(camera, previous_camera_error);
        std::cout << "User> " << std::flush;
        if (!readLine(question)) break;
        question = trim(question);
        if (question == "exit" || question == "quit") break;
        if (question.empty()) continue;
        if (question == "stats") { showStats(camera, frames); continue; }
        const auto response = processor.process(question);
        std::cout << "Request Type: " << requestTypeName(response.route.type)
                  << " (" << response.route.reason << ")\n";
        const auto& result = response.inference;
        if (response.route.type == RequestType::Vision)
            std::cout << "Frame ID: " << result.frame_id
                      << ", Frame Timestamp(ns): " << result.frame_timestamp_ns
                      << ", Frame Age: " << result.frame_age_ms << " ms\n";
        if (result.success) std::cout << "Assistant> " << result.text << '\n';
        else std::cout << "Error> " << result.error_message << '\n';
        std::cout << "Request Latency: " << result.latency_ms << " ms\n";
    }
    std::cout << "\nExiting.\n";
    return 0;
}
