#pragma once
#include <atomic>
#include <cstdint>
#include <gst/gst.h>

#include "config_manager.h"

class AudioSource {
    GstElement* pipeline_;
    AudioSettings settings_;
    GstElement* audio_bin_{ nullptr };
    GstElement* audio_tee_{ nullptr };
    std::atomic<uint32_t> frame_count_{ 0 };

    static GstPadProbeReturn buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
public:
    AudioSource(GstElement* p, const AudioSettings& settings);
    bool create();
    bool update(const AudioSettings& settings);
    uint32_t consumeFrameCount();
    GstElement* get_tee();
    bool operator==(const GstElement* other) const;
};
