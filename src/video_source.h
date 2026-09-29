#pragma once

#include <atomic>
#include <cstdint>
#include <config_manager.h>
#include <preview_state.h>
#include <media_capture.h>
#include <media_pipeline.h>
#include <condition_variable>

class VideoSource {
    GstElement* stream_bin_;
    VideoSettings settings_; 
    GstElement* video_bin_{ nullptr };
    GstElement* tee_{ nullptr };
    std::shared_ptr<MediaCapture> capture_;
    GstPad* ghost_pad_{ nullptr };
    GstPad* src_pad_{ nullptr };

    std::mutex mutex_;
    std::condition_variable unlink_cv_;

    void destroy();
    static GstPadProbeReturn unlink_cb(GstPad* pad, GstPadProbeInfo*, gpointer user_data);
    void unlink();
    static GstElement* createEncoder(const VideoSettings& settings);
public:
    VideoSource(GstElement* stream_bin, const VideoSettings& settings);
    ~VideoSource();
    bool create(std::shared_ptr<MediaCapture>& capture);
    bool update(const VideoSettings& settings);

    GstPad* getNextSrcPad();
    void removeSrcPad(GstPad* src_pad);
};
