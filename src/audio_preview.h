#pragma once
#include <memory>
#include <gst/gst.h>
#include <preview_state.h>

class MediaPipeline;
class MediaCapture;

class AudioPreview {
    MediaPipeline* pipeline_{ nullptr };
    GstElement* bin_{ nullptr };

    std::shared_ptr<AudioPreviewBuffer> buffer_;

    std::shared_ptr<MediaCapture> capture_;
    GstPad* ghost_pad_{ nullptr };
    GstPad* src_pad_{ nullptr };

    void destroy();

public:
    AudioPreview(MediaPipeline* pipeline);
    ~AudioPreview();

    bool create(GstDevice* device, std::shared_ptr<AudioPreviewBuffer>& buffer);

    static GstFlowReturn onPreviewSample(GstElement* sink, gpointer user_data);
};