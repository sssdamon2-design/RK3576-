#include "video/camera_service.h"
#include <exception>
#include <utility>

CameraService::CameraService(const std::string& device, uint32_t width,
                             uint32_t height, uint32_t pixel_format,
                             LatestFrameBuffer& latest_frame_buffer)
    : camera_(device, width, height, pixel_format),
      latest_frame_buffer_(latest_frame_buffer) {}
CameraService::~CameraService() { stop(); }

bool CameraService::start()
{
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_.load()) return true;
    // 出错退出的线程也必须 join，才能重新赋值 std::thread。
    if (capture_thread_.joinable()) capture_thread_.join();
    camera_.shutdown();
    latest_frame_buffer_.clear();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = {};
        started_at_ = {};
        stopped_at_ = {};
    }
    try
    {
        if (!camera_.initialize())
        {
            setError("Camera initialization failed: " + camera_.lastError());
            camera_.shutdown();
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            started_at_ = std::chrono::steady_clock::now();
        }
        running_.store(true);
        capture_thread_ = std::thread(&CameraService::captureLoop, this);
        return true;
    }
    catch (const std::exception& e)
    {
        setError(std::string("Camera initialization/thread start exception: ") + e.what());
    }
    catch (...) { setError("Camera initialization/thread start: unknown exception"); }
    running_.store(false);
    camera_.shutdown();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stopped_at_ = std::chrono::steady_clock::now();
    }
    return false;
}

void CameraService::stop()
{
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    running_.store(false);
    // join 后才释放设备，不与采集线程并发 close/munmap。
    if (capture_thread_.joinable()) capture_thread_.join();
    camera_.shutdown();
    latest_frame_buffer_.clear();
}
bool CameraService::isRunning() const { return running_.load(); }
void CameraService::setError(const std::string& message)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.last_error = message;
}
CameraStats CameraService::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    CameraStats result = stats_;
    result.running = running_.load();
    if (started_at_ != std::chrono::steady_clock::time_point{})
    {
        const auto end = stopped_at_ == std::chrono::steady_clock::time_point{}
            ? std::chrono::steady_clock::now() : stopped_at_;
        const double seconds = std::chrono::duration<double>(end - started_at_).count();
        if (seconds > 0) result.approximate_fps = result.frame_count / seconds;
    }
    return result;
}
void CameraService::captureLoop()
{
    try
    {
        while (running_.load())
        {
            ImageFrame frame;
            const auto status = camera_.captureFrame(frame, 200);
            if (status == VideoCapture::CaptureStatus::Timeout)
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.timeout_count;
                continue;
            }
            if (status == VideoCapture::CaptureStatus::Error)
            {
                setError("Camera capture failed: " + camera_.lastError());
                break;
            }
            if (!running_.load()) break;
            // move 转移 vector 所有权，不再复制整幅图像。
            latest_frame_buffer_.update(std::make_shared<const ImageFrame>(std::move(frame)));
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.frame_count;
        }
    }
    catch (const std::exception& e) { setError(std::string("Camera capture exception: ") + e.what()); }
    catch (...) { setError("Camera capture: unknown exception"); }
    latest_frame_buffer_.clear();
    camera_.shutdown();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stopped_at_ = std::chrono::steady_clock::now();
    }
    running_.store(false);
}
