#pragma once

#include <atomic>
#include <cstdint>
#include "config_manager.h"

class StreamStateStore;

class VideoSource {
    const GstElement* pipeline;
    VideoSettings settings;
    StreamStateStore* stream_states_;
    const std::string stream_id_;
    GstElement* video_bin{ nullptr };
    GstElement* video_tee{ nullptr };
    std::atomic<uint32_t> frame_count{ 0 };
    uint64_t preview_index_{0};

    GstElement* capture_tee_{ nullptr };
    GstElement* preview_bin_{ nullptr };
    GstPad* preview_tee_pad_{ nullptr };

    std::atomic<bool> preview_starting_{false};
    std::atomic<bool> preview_stopping_{ false };

    GstElement* create_preview();

    static GstPadProbeReturn process_jpeg(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn start_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn stop_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    bool add_preview_branch();
    void remove_preview_branch();

    void setVideoPreview(const uint8_t* buffer, const size_t buffer_size);
public:
    VideoSource(const GstElement* p, const VideoSettings& vs, StreamStateStore* stream_states, const std::string& stream_id);
    bool create();
    bool update(const VideoSettings& vs);

    void startVideoPreview();
    void stopVideoPreview();

    bool operator==(GstElement* other) const;

    uint32_t consumeFrameCount();
    GstElement* get_tee();
};
