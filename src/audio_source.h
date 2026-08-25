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
    GstElement* fakesink_{ nullptr };
    std::atomic<uint32_t> frame_count_{ 0 };
    GstPad* source_ghost_pad_{ nullptr };
    gulong source_probe_id_{ 0 };

    static GstPadProbeReturn buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn capture_pad_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
public:
    AudioSource(GstElement* p, const AudioSettings& settings);
    ~AudioSource();
    bool create();
    bool update(const AudioSettings& settings);
    void updateStats(std::shared_ptr<StreamStatus>& stats);
    GstElement* get_tee();
    bool operator==(const GstElement* other) const;
};
