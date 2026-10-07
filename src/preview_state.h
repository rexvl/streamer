#pragma once
#include <memory>
#include <atomic>
#include <vector>
#include <shared_mutex>
#include <gst/gst.h>

class PreviewUpdateListener {
public:
    virtual void onPreviewUpdated() = 0;
};

class VideoPreviewFrame {
    GstSample* sample_;
    const uint64_t preview_index_;
    GstBuffer* buffer_{nullptr};
    GstMapInfo map_{ };
public:
    VideoPreviewFrame(GstSample* sample, const uint64_t preview_index) :
        sample_(sample),
        preview_index_(preview_index) {
        buffer_ = gst_sample_get_buffer(sample_);
        if (buffer_) {
            if (!gst_buffer_map(buffer_, &map_, GST_MAP_READ)) {
                gst_sample_unref(sample_);
                sample_ = nullptr;
                buffer_ = nullptr;
            }
        }
    }

    ~VideoPreviewFrame() {
        if (buffer_) {
            gst_buffer_unmap(buffer_, &map_);
        }
        if (sample_) {
            gst_sample_unref(sample_);
        }
    }

    const uint8_t* data() const {
        return buffer_ ? map_.data : nullptr;
    }

    size_t size() const {
        return buffer_ ? map_.size : 0;
    }

    bool empty() const { 
        return buffer_ == nullptr || map_.size == 0;
    }

    uint64_t index() const {
        return preview_index_;
    }
};

class BasePreviewBuffer {
    PreviewUpdateListener* listener_;
    std::atomic<uint32_t> num_clients_{ 0 };
    std::atomic<uint64_t>& previews_version_;

public:
    BasePreviewBuffer(PreviewUpdateListener* listener, std::atomic<uint64_t>& previews_version) :
        listener_(listener),
        previews_version_(previews_version) {
    }

    void notifyPreviewUpdate() {
        listener_->onPreviewUpdated();
    }

    void addClient() {
        if (num_clients_.fetch_add(1, std::memory_order_relaxed) == 0) {
            previews_version_.fetch_add(1, std::memory_order_release);
        }
    }

    void removeClient() {
        if (num_clients_.fetch_sub(1, std::memory_order_relaxed) == 1) {
            previews_version_.fetch_add(1, std::memory_order_release);
        }
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
    VideoPreviewBuffer(PreviewUpdateListener* listener, std::atomic<uint64_t>& previews_version) :
        BasePreviewBuffer(listener, previews_version) {
    }

    void setPreview(GstSample* sample) {
        auto preview = std::make_shared<VideoPreviewFrame>(sample, ++preview_frame_index_);

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
    AudioPreviewBuffer(PreviewUpdateListener* listener, std::atomic<uint64_t>& previews_version) :
        BasePreviewBuffer(listener, previews_version) {
    }
};