#pragma once
#include <atomic>
#include <cstdint>
#include <future>
#include <gst/gst.h>
#include "config_manager.h"

class MediaOutput {
    GstElement* pipeline_;
    OutputSettings settings_;
    GstElement* output_bin_{nullptr};
    GstElement* mux_{ nullptr };
    std::chrono::steady_clock::time_point last_stats_time_;
    std::atomic<uint64_t> bytes_count_{ 0 };
    std::atomic<uint32_t> packet_count_{ 0 };
    GstPad* video_tee_pad_{ nullptr };
    GstPad* audio_tee_pad_{ nullptr };
    bool video_tee_blocked_{ false };
    bool audio_tee_blocked_{ false };
    gulong audio_probe_id_{ 0 };
    gulong video_probe_id_{ 0 };
    // Pads and probe ids for internal branches so we can remove probes on teardown
    GstPad* sink_pad_{ nullptr };
    gulong sink_probe_id_{ 0 };
    GstPad* vqueue_src_pad_{ nullptr };
    gulong vqueue_probe_id_{ 0 };
    GstPad* aqueue_src_pad_{ nullptr };
    gulong aqueue_probe_id_{ 0 };
    std::future<void> restart_future_;
    uint64_t passed_count_{ 0 };
    bool waiting_for_keyframe_{ true };
    GstElement* sink_{ nullptr };
    GstElement* vqueue_{ nullptr };

    std::atomic<uint64_t> vqueue_dropped_{ 0 };
    uint64_t last_vqueue_dropped_{ 0 };

    std::atomic<uint64_t> aqueue_dropped_{ 0 };
    uint64_t last_aqueue_dropped_{ 0 };

    static GstPadProbeReturn sink_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn drop_until_keyframe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    static void on_vqueue_overrun(GstElement* queue, gpointer user_data);
    static void on_aqueue_overrun(GstElement* queue, gpointer user_data);

    static GstPadProbeReturn tee_pad_block(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    void restart();
public:
    MediaOutput(GstElement* p, const OutputSettings& s);
    ~MediaOutput();

    bool create();

    bool addVideo(GstElement* video_tee);
    bool addAudio(GstElement* audio_tee);
    bool syncState();

    bool blockTeePads();

    void updateStats(std::shared_ptr<StreamStatus>& stats);

    GstElement* getElement() const;
};
