#pragma once
#include <memory>
#include <gst/gst.h>
#include <preview_state.h>

class MediaPipeline;

class MediaCapture {
    MediaPipeline* pipeline_;
    GstElement* bin_{nullptr};
    GstElement* tee_{nullptr};


    GstPad* getNextSrcPad();
public:
    MediaCapture(MediaPipeline* pipeline);
    ~MediaCapture();
    bool create(GstDevice* device);
    GstPad* linkNextSrcPad(GstPad* other_pad);
    void removeSrcPad(GstPad* src_pad);
};
