#pragma once
#include <atomic>
#include <cstdint>
#include <gst/gst.h>
#include "config_manager.h"

struct MediaOutput {
    GstElement* pipeline;
    OutputSettings settings;
    GstElement* output_bin{nullptr};
    GstElement* mux{nullptr};
    std::atomic<uint32_t> packet_count{ 0 };

    MediaOutput(GstElement* p, const OutputSettings& s);
    static GstPadProbeReturn sink_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    bool create();
    bool addVideo(GstElement* video_tee);
    bool addAudio(GstElement* audio_tee);
};
