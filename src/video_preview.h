#pragma once
#include <mutex>
#include <condition_variable>
#include <media_pipeline.h>
#include <preview_state.h>

class VideoPreview {
    MediaPipeline* pipeline_;
    GstElement* bin_{nullptr};

    std::shared_ptr<VideoPreviewBuffer> buffer_;

    std::shared_ptr<MediaCapture> capture_;
    GstPad* ghost_pad_{ nullptr };
    GstPad* src_pad_{ nullptr };

    void destroy();
public:
    VideoPreview(MediaPipeline* pipeline);
    ~VideoPreview();
    bool create(GstDevice* device, std::shared_ptr<VideoPreviewBuffer>& buffer);

    static GstFlowReturn onPreviewFrame(GstElement* sink, gpointer user_data);
};
 