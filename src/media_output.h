#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <gst/gst.h>
#include <config_manager.h>
#include <media_pipeline.h>

class VideoSource;

class MediaOutput {
    GstElement* stream_bin_;
    OutputSettings settings_;
    GstElement* output_bin_{nullptr};
    GstElement* mux_{ nullptr };
    GstPad* sink_ghost_{ nullptr };
    GstPad* video_tee_pad_{ nullptr };
    GstElement* vqueue_{ nullptr };
    std::mutex mutex_;
    std::condition_variable cv_;
    VideoSource* video_{nullptr};

    void unlink();
public:
    MediaOutput(GstElement* stream_bin, const OutputSettings& s);
    ~MediaOutput();

    bool create();

    bool addVideo(VideoSource* video);
    //bool addAudio(GstElement* audio_tee);
    bool syncState();

    GstElement* getElement() const;
};
