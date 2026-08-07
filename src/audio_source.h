#pragma once
#include <atomic>
#include <cstdint>
#include <gst/gst.h>

#include "config_manager.h"

struct AudioSource {
    GstElement* pipeline;
    AudioSettings settings;
    GstElement* audio_bin{ nullptr };
    GstElement* audio_tee{ nullptr };
    std::atomic<uint32_t> frame_count{ 0 };
    SourceStatus status;

    AudioSource(GstElement* p, const AudioSettings& as);

    static GstPadProbeReturn buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    bool create();
};
