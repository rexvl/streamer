#pragma once

#include <string>
#include <map>
#include <memory>

#include "config_manager.h"

class VideoSource;
class AudioSource;
struct MediaOutput;


struct MediaStream {
    const std::string id_;
    GstElement* pipeline{nullptr};
    GstBus *bus{nullptr};
    std::unique_ptr<VideoSource> video;
    std::unique_ptr<AudioSource> audio;
    std::map<std::string, std::unique_ptr<MediaOutput>> outputs;
    bool playing_{ false };
    int inactivity_count_{ 0 };

    MediaStream(const std::string& id);

    bool create();
    bool start();
    bool addVideo(const VideoSettings& settings);
    bool addAudio(const AudioSettings& settings);
    bool IsSourcesEmpty();
    bool addOutput(const std::string& id, const OutputSettings& settings);
    bool IsOutputsEmpty();
    bool removeVideo();
    bool removeAudio();
    bool syncOutputs(std::map<std::string, OutputSettings> settings);
    bool onError(GstElement* src);
    bool ProcessMessage();

    ~MediaStream();
};
