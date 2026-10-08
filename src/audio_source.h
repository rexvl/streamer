#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <gst/gst.h>

#include <config_manager.h>
#include <media_capture.h>
#include <media_pipeline.h>

class AudioSource {
    GstElement* stream_bin_;
    AudioSettings settings_;
    GstElement* audio_bin_{ nullptr };
    GstElement* tee_{ nullptr };
    std::shared_ptr<MediaCapture> capture_;
    GstPad* ghost_pad_{ nullptr };
    GstPad* src_pad_{ nullptr };

    void destroy();
    static GstElement* createEncoder(const AudioSettings& settings);
public:
    AudioSource(GstElement* stream_bin, const AudioSettings& settings);
    ~AudioSource();
    bool create(std::shared_ptr<MediaCapture>& capture);
    bool update(const AudioSettings& settings);

    GstPad* getNextSrcPad();
    void removeSrcPad(GstPad* src_pad);
};
