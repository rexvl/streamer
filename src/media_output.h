#pragma once
#include <atomic>
#include <cstdint>
#include <future>
#include <gst/gst.h>
#include "config_manager.h"

struct MediaOutput {
    GstElement* pipeline_;
    OutputSettings settings_;
    GstElement* output_bin_{nullptr};
    GstElement* mux_{ nullptr };
    std::atomic<uint32_t> packet_count_{ 0 };
    GstPad* video_tee_pad_{ nullptr };
    GstPad* audio_tee_pad_{ nullptr };
    bool video_tee_blocked_{ false };
    bool audio_tee_blocked_{ false };
    gulong audio_probe_id_{ 0 };
    std::future<void> restart_future_;
    uint64_t passed_count_{ 0 };

    MediaOutput(GstElement* p, const OutputSettings& s);
    static GstPadProbeReturn sink_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn queue_output_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    bool create();
    bool addVideo(GstElement* video_tee);
    bool addAudio(GstElement* audio_tee);

    bool blockTeePads();
    static GstPadProbeReturn tee_pad_block(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    void restart();
};
