#pragma once

#include <atomic>
#include <cstdint>
#include "config_manager.h"

struct VideoSource {
    GstElement* pipeline;
    VideoSettings settings;
    GstElement* video_bin{ nullptr };
    GstElement* video_tee{ nullptr };
    std::atomic<uint32_t> frame_count{ 0 };
    SourceStatus status;

    VideoSource(GstElement* p, const VideoSettings& vs);
    bool create();
};
