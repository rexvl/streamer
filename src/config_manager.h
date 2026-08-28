#pragma once
#include <string>
#include <map>
#include <set>
#include <atomic>
#include <shared_mutex>

#include <preview_state.h>

#include <gst/gst.h>

enum class SourceStatus {
    kDisabled,
    kUnknown,
    kUnavailable,
    kSuccess,
    kFail
};

struct VideoSettings {
    std::string device_id;
    GstDevice* device{ nullptr };

    int width{ 1920 };
    int height{ 1080 };
    int fps_n{ 30 };
    int fps_d{ 1 };

    enum class Codec { x264enc, nvautogpuh264enc, qsvh264enc };
    Codec codec{ Codec::x264enc };

    int bitrate{ 1000 }; // kbps

    bool operator==(const VideoSettings & other) {
        return device_id == other.device_id &&
               device == other.device &&
               width  == other.width &&
               height == other.height &&
               fps_n  == other.fps_n &&
               fps_d  == other.fps_d &&
               codec  == other.codec &&
               bitrate == other.bitrate;
    }
};

struct AudioSettings {
    std::string device_id;
    GstDevice* device{ nullptr };

    int sampleRate{ 48000 };
    int channel_count{ 2 };

    enum class Codec { AAC };
    Codec codec { Codec::AAC };

    int bitrate{ 128 };

    bool operator==(const AudioSettings& other) {
        return device_id == other.device_id &&
            device == other.device &&
            sampleRate == other.sampleRate &&
            channel_count == other.channel_count &&
            codec == other.codec &&
            bitrate == other.bitrate;
    }
};

enum class OutputStatus {
    kUnknown,
    kSuccess,
    kFail
};

struct OutputSettings {
    std::string id;
    bool enabled{ true };
    std::string url;
};

struct StreamSettings {
    std::string id;
    std::shared_ptr<VideoSettings> video;
    std::shared_ptr<AudioSettings> audio;
    std::map<std::string, OutputSettings> outputs;

    bool isEnabled() const {
        for (const auto& [_, output] : outputs) {
            if (output.enabled) {
                return true;
            }
        }

        return false;
    }
/*
    bool isVideoAvailable() {
        return (video && video->device);
    }

    bool isAudioAvailable() {
        return (audio && audio->device);
    }
*/
};

struct DeviceInfo {
    std::string name_;
    GstDevice* device_;

    DeviceInfo(const std::string& name, GstDevice* device) :
        name_(name), device_(device) {
    }
};

class StreamStatus {
    std::atomic<SourceStatus> video_status_{ SourceStatus::kUnknown };
    std::atomic<SourceStatus> audio_status_{ SourceStatus::kUnknown };

    std::shared_mutex output_mutex_;
    std::map<std::string, OutputStatus> output_status_;
public:
    StreamStatus() = default;

    void setVideoStatus(SourceStatus status) {
        video_status_.store(status, std::memory_order_relaxed);
    }

    SourceStatus getVideoStatus() const {
        return video_status_.load(std::memory_order_relaxed);
    }

    void setAudioStatus(SourceStatus status) {
        audio_status_.store(status, std::memory_order_relaxed);
    }

    SourceStatus getAudioStatus() const {
        return audio_status_.load(std::memory_order_relaxed);
    }

    void setOutputStatus(const std::string& id, OutputStatus status) {
        std::unique_lock<std::shared_mutex> lk(output_mutex_);
        output_status_[id] = status;
    }

    std::map<std::string, OutputStatus> getOutputStatus() {
        std::shared_lock<std::shared_mutex> lk(output_mutex_);
        return output_status_;
    }
};

struct StreamContext {
    StreamSettings settings;
    std::shared_ptr<PreviewState> preview;
    std::shared_ptr<StreamStatus> status;

    StreamContext() = default;

    StreamContext(const StreamSettings& settings, PreviewUpdateListener* preview_listener) :
        settings(settings) {
        preview = std::make_shared<PreviewState>(preview_listener);
        status  = std::make_shared<StreamStatus>();
    }
};

class ConfigManager {
    PreviewUpdateListener* preview_listener_;

    std::shared_mutex mutex_;
    std::map<std::string, StreamContext> streams_;

    std::map<GstDevice*, std::set<std::string>> video_streams_index_;
    std::map<GstDevice*, std::set<std::string>> audio_streams_index_;

    std::map<std::string, std::shared_ptr<DeviceInfo>> video_devices_;
    std::map<std::string, std::shared_ptr<DeviceInfo>> audio_devices_;
    uint64_t next_stream_id_{0};

    void addStream(std::map<std::string, std::set<std::string>>& device_streams,
                   const std::string& device_id, const std::string& stream_id);

    void addStreamIndex(StreamContext& settings);

    void removeStreamIndex(const StreamSettings& settings);

    void removeStreamIndex(std::map<GstDevice*, std::set<std::string>>& stream_index,
                           GstDevice* device_id, const std::string& stream_id);

    bool isActive(StreamSettings& settings);

    std::shared_ptr<StreamContext> getContext(const std::string& id);

    ConfigManager() = default;
public:
    static ConfigManager& getInstance();
    void setPreviewListener(PreviewUpdateListener* listener);
    void load();
    void getStreams(std::map<std::string, StreamSettings>& streams);
    bool getStream(StreamSettings& stream, const std::string& id);
    bool addStream(StreamSettings& stream);
    bool updateStream(const StreamSettings& settings);
    bool removeStream(const std::string& id);

    // to get enabeld streams and ouputs only
    void getActiveStreams(std::map<std::string, StreamContext>& streams);

    // device api
    void addVideoDevice(const std::string& id, const std::string& name, GstDevice* device);
    void addAudioDevice(const std::string& id, const std::string& name, GstDevice* device);
    void removeVideoDevice(const std::string& id);
    void removeAudioDevice(const std::string& id);
    void getVideoDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& video_devices);
    void getAudioDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& audip_devices);
    GstDevice* getVideoDevice(const std::string& id);
    GstDevice* getAudioDevice(const std::string& id);

    std::shared_ptr<PreviewState> getPreviewState(const std::string& stream_id);

    // status api
    void getStreamsStatus(std::map<std::string, std::shared_ptr<StreamStatus>>& stream_status);
    std::shared_ptr<StreamStatus> getStreamStatus(const std::string& id);
};