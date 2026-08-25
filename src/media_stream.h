#pragma once

#include <string>
#include <map>
#include <memory>
#include <preview_state.h>
#include <config_manager.h>

class StreamStateStore;

class VideoSource;
class AudioSource;
struct MediaOutput;

struct MediaStream {
    std::shared_ptr<PreviewState> preview_;
    std::shared_ptr<StreamStatus> status_;
    GstElement* pipeline{nullptr};
    GstBus *bus{nullptr};
    std::unique_ptr<VideoSource> video;
    std::unique_ptr<AudioSource> audio;
    std::map<std::string, std::unique_ptr<MediaOutput>> outputs;
    bool playing_{ false };
    int inactivity_count_{ 0 };

    MediaStream(const std::shared_ptr<PreviewState>& preview,
                const std::shared_ptr<StreamStatus>& status);

    static GstBusSyncReply bus_sync_handler(GstBus* bus, GstMessage* message, gpointer user_data);

    bool create(const StreamSettings& settings);
    bool start();

    bool update(const StreamSettings& settings);
    bool addVideo(const VideoSettings& settings);
    bool addAudio(const AudioSettings& settings);
    //bool IsSourcesEmpty();
    bool addOutput(const std::string& id, const OutputSettings& settings, bool sync_state);
    bool IsOutputsEmpty();
    bool removeVideo();
    bool removeAudio();
    bool syncOutputs(std::map<std::string, OutputSettings> settings, bool sync_state);
    bool onError(GstElement* src);
    bool ProcessMessage(uint64_t mask = GST_MESSAGE_INFO | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ELEMENT);
    bool ProcessError();
    void syncPreview();
    void updateStatus();

    ~MediaStream();
};