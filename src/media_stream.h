#pragma once

#include <string>
#include <map>
#include <memory>
#include <preview_state.h>
#include <config_manager.h>
#include <media_pipeline.h>

class StreamStateStore;

class VideoSource;
class AudioSource;
struct MediaOutput;

struct MediaStream {
    MediaPipeline* pipeline_;
    GstElement* bin_{nullptr};
    GstBus *bus{nullptr};
    std::unique_ptr<VideoSource> video;
    std::unique_ptr<AudioSource> audio;
    std::map<std::string, std::unique_ptr<MediaOutput>> outputs;
    bool playing_{ false };
    int inactivity_count_{ 0 };

    MediaStream(MediaPipeline* pipeline);
    ~MediaStream();

    bool create(const StreamSettings& settings);
    bool update(const StreamSettings& settings);
    bool addVideo(const VideoSettings& settings);
    //bool addAudio(const AudioSettings& settings);
    //bool IsSourcesEmpty();
    bool addOutput(const std::string& id, const OutputSettings& settings);
    bool removeVideo();
    bool removeAudio();
    bool syncOutputs(std::map<std::string, OutputSettings> settings);
    GstElement* createChildBin();
    GstElement* getElement();

    bool onError(GstElement* element);
};