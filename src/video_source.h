#pragma once

#include <atomic>
#include <cstdint>
#include "config_manager.h"

class StreamStateStore;

struct VideoSource {
    const GstElement* pipeline;
    VideoSettings settings;
    StreamStateStore* stream_states_;
    const std::string stream_id_;
    GstElement* video_bin{ nullptr };
    GstElement* video_tee{ nullptr };
    std::atomic<uint32_t> frame_count{ 0 };
    SourceStatus status;
    uint64_t preview_index_{0};

    VideoSource(const GstElement* p, const VideoSettings& vs, StreamStateStore* stream_states, const std::string& stream_id);
    bool create();

    GstElement* create_preview(GstElement* video_bin);

    static GstPadProbeReturn process_jpeg(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    void setVideoPreview(const uint8_t* buffer, const size_t buffer_size);

    void startVideoPreview();
    void stopVideoPreview();
};
