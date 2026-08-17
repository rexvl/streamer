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

class PreviewState {
    std::atomic<uint32_t> num_video_clients{ 0 };
    std::shared_ptr<const VideoPreview> preview_;
    std::atomic<double> audio_level_{ 0.0 };
public:
    PreviewState() = default;

    void addVideoClient() {
        num_video_clients.fetch_add(1, std::memory_order_relaxed);
    }

    void removeVideoClient() {
        num_video_clients.fetch_sub(1, std::memory_order_relaxed);
    }

    bool isVideoPreviewEnabled() const {
        return num_video_clients.load(std::memory_order_relaxed) != 0;
    }

    void setPreview(std::shared_ptr<const VideoPreview> preview) {
        std::atomic_store_explicit(&preview_, std::move(preview), std::memory_order_release);
    }

    std::shared_ptr<const VideoPreview> getPreview() const {
        return std::atomic_load_explicit(&preview_, std::memory_order_acquire);
    }

    double getAudioLevel() const {
        return audio_level_.load(std::memory_order_relaxed);
    }

    void setAudioLevel(double level) {
        audio_level_.store(level, std::memory_order_relaxed);
    }
};
