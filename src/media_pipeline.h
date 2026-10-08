#pragma once
#include <map>
#include <string>
#include <memory>
#include <gst/gstelement.h>
#include <config_manager.h>
#include <media_capture.h>
#include <video_preview.h>

class MediaStream;
class VideoPreview;

class MediaPipeline {
    GstElement* pipeline_{ nullptr };
    GstBus* bus_{nullptr};
    GstState state_{GST_STATE_NULL};

    struct AudioCapture {
        GstElement* capture{ nullptr };
        GstElement* tee{ nullptr };
    };

    std::map<GstDevice*, std::weak_ptr<MediaCapture>> video_captures_;
    std::map<GstDevice*, std::weak_ptr<MediaCapture>> audio_captures_;

    std::map<std::string, std::unique_ptr<MediaStream>> streams_;

    std::map<GstDevice*, std::shared_ptr<VideoPreview>> video_previews_;
    //std::map<GstDevice*, std::shared_ptr<MediaCapture>> audio_previews_;
public:
    MediaPipeline();
    ~MediaPipeline();

    bool create();

    GstElement* getElement();
    GstElement* createChildBin();

    std::shared_ptr<MediaCapture> ensureVideoCapture(GstDevice* device);
    std::shared_ptr<MediaCapture> ensureAudioCapture(GstDevice* device);
    std::shared_ptr<MediaCapture> getAudioCapture(GstDevice* device);

    bool ProcessMessage();

    bool IsPlaying();

    bool syncStreams(std::map<std::string, StreamSettings>& streams);

    void syncVideoPreviews(std::map<GstDevice*, std::shared_ptr<VideoPreviewBuffer>>& video_previews);
};