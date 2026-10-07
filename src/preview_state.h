#pragma once
#include <memory>
#include <atomic>
#include <vector>
#include <shared_mutex>

class PreviewUpdateListener {
public:
    virtual void onPreviewUpdated() = 0;
};

struct VideoPreviewFrame {
    std::vector<uint8_t> data_;
    uint64_t preview_index_;
    VideoPreviewFrame(const uint8_t* buffer, const size_t buffer_size, const uint64_t preview_index) :
        data_(buffer, buffer + buffer_size),
        preview_index_(preview_index) {
    }
};

class BasePreviewBuffer {
    PreviewUpdateListener* listener_;
    std::atomic<uint32_t> num_clients_{ 0 };

public:
    BasePreviewBuffer(PreviewUpdateListener* listener) :
        listener_(listener) {
    }

    void notifyPreviewUpdate() {
        listener_->onPreviewUpdated();
    }

    void addClient() {
        num_clients_.fetch_add(1, std::memory_order_relaxed);
    }

    void removeClient() {
        num_clients_.fetch_sub(1, std::memory_order_relaxed);
    }

    bool isPreviewEnabled() const {
        return num_clients_.load(std::memory_order_relaxed) != 0;
    }
};

class VideoPreviewBuffer : public BasePreviewBuffer {
    mutable std::shared_mutex mutex_;
    std::shared_ptr<const VideoPreviewFrame> preview_;
    uint64_t preview_frame_index_{ 0 };
public:
    VideoPreviewBuffer(PreviewUpdateListener* listener) :
        BasePreviewBuffer(listener) {
    }

    void setPreview(const uint8_t* buffer, const size_t buffer_size) {
        auto preview = std::make_shared<VideoPreviewFrame>(buffer, buffer_size, ++preview_frame_index_);

        std::unique_lock<std::shared_mutex> write_lock(mutex_);
        preview_ = preview;
        write_lock.unlock();

        notifyPreviewUpdate();
    }

    std::shared_ptr<const VideoPreviewFrame> getPreview() const {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        return preview_;
    }
};

class AudioPreviewBuffer : public BasePreviewBuffer {
public:
    AudioPreviewBuffer(PreviewUpdateListener* listener) :
        BasePreviewBuffer(listener) {
    }
};