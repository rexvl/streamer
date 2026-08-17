#pragma once
#include <memory>
#include <atomic>
#include <vector>

struct VideoPreview {
    std::vector<uint8_t> data_;
    uint64_t preview_index_;
    VideoPreview(const uint8_t* buffer, const size_t buffer_size, const uint64_t preview_index) :
        data_(buffer, buffer + buffer_size),
        preview_index_(preview_index) {
    }
};

class PreviewUpdateListener {
public:
    virtual void onPreviewUpdated() = 0;
};

class PreviewState {
    PreviewUpdateListener* listener_;
    std::atomic<uint32_t> num_video_clients{ 0 };
    std::shared_ptr<const VideoPreview> preview_;
    std::atomic<uint32_t> num_audio_clients{ 0 };
    std::atomic<double> audio_level_{ 0.0 };
public:
    PreviewState(PreviewUpdateListener* listener) :
        listener_(listener) {
    }

    void addVideoClient() {
        num_video_clients.fetch_add(1, std::memory_order_relaxed);
    }

    void removeVideoClient() {
        num_video_clients.fetch_sub(1, std::memory_order_relaxed);
    }

    bool isVideoPreviewEnabled() const {
        return num_video_clients.load(std::memory_order_relaxed) != 0;
    }

    void addAudioClient() {
        num_audio_clients.fetch_add(1, std::memory_order_relaxed);
    }

    void removeAudioClient() {
        num_audio_clients.fetch_sub(1, std::memory_order_relaxed);
    }

    bool isAudioPreviewEnabled() const {
        return num_audio_clients.load(std::memory_order_relaxed) != 0;
    }

    void setPreview(std::shared_ptr<const VideoPreview> preview) {
        std::atomic_store_explicit(&preview_, std::move(preview), std::memory_order_release);
        listener_->onPreviewUpdated();
    }

    std::shared_ptr<const VideoPreview> getPreview() const {
        return std::atomic_load_explicit(&preview_, std::memory_order_acquire);
    }

    double getAudioLevel() const {
        return audio_level_.load(std::memory_order_relaxed);
    }

    void setAudioLevel(double level) {
        audio_level_.store(level, std::memory_order_relaxed);
        listener_->onPreviewUpdated();
    }
};
