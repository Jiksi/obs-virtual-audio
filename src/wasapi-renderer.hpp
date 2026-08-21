#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class AudioCapture;

class WasapiRenderer {
public:
    explicit WasapiRenderer(AudioCapture &capture);
    ~WasapiRenderer();

    WasapiRenderer(const WasapiRenderer &) = delete;
    WasapiRenderer &operator=(const WasapiRenderer &) = delete;

    bool start();
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] uint64_t rendered_frames() const noexcept;
    [[nodiscard]] uint64_t underrun_frames() const noexcept;

private:
    void run() noexcept;
    void report_initialization(bool succeeded) noexcept;

    AudioCapture &capture_;
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
