#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioCapture;

struct WasapiDevice {
    std::string id;
    std::string name;
};

class WasapiRenderer {
public:
    WasapiRenderer(AudioCapture &capture, std::string target_device_id = {});
    ~WasapiRenderer();

    WasapiRenderer(const WasapiRenderer &) = delete;
    WasapiRenderer &operator=(const WasapiRenderer &) = delete;

    bool start();
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] uint64_t rendered_frames() const noexcept;
    [[nodiscard]] uint64_t underrun_frames() const noexcept;
    [[nodiscard]] const std::string &active_device_id() const noexcept;
    [[nodiscard]] const std::string &active_device_name() const noexcept;

    [[nodiscard]] static std::vector<WasapiDevice> enumerate_devices();

private:
    void run() noexcept;
    void report_initialization(bool succeeded) noexcept;

    AudioCapture &capture_;
    std::string target_device_id_;
    std::string active_device_id_;
    std::string active_device_name_;
    void *stop_event_ = nullptr;
    void *audio_event_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> rendered_frames_{0};
    std::atomic<uint64_t> underrun_frames_{0};

    std::mutex initialization_mutex_;
    std::condition_variable initialization_cv_;
    bool initialization_complete_ = false;
    bool initialization_succeeded_ = false;
};
