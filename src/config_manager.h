#pragma once
#include <string>
#include <map>
#include <set>
#include <shared_mutex>

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

    int width{ 0 };
    int height{ 0 };
    int fps_n{ 0 };
    int fps_d{ 1 };

    enum class Codec { AVC, HEVC };
    Codec codec;

    int bitrate{ 0 };

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

    int sampleRate{ 0 };
    int channel_count{ 0 };

    enum class Codec { AAC };
    Codec codec;

    int bitrate{ 0 };

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
    enum class Type { RTSP, RTMP };
    Type type;
    std::string url;
};

struct StreamSettings {
    std::string id;
    std::shared_ptr<VideoSettings> video;
    std::shared_ptr<AudioSettings> audio;
    std::map<std::string, OutputSettings> outputs;

    bool isEnabled() const {
        for (const auto& it : outputs) {
            auto& output = it.second;
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

struct StreamStatus {
    std::string id;
    SourceStatus video_status{ SourceStatus::kUnknown };
    SourceStatus audio_status{ SourceStatus::kUnknown };
    std::map<std::string, OutputStatus> output_status;
};

class ConfigManager {
    std::shared_mutex mutex_;
    std::map<std::string, StreamSettings> streams_;

    std::map<GstDevice*, std::set<std::string>> video_streams_index_;
    std::map<GstDevice*, std::set<std::string>> audio_streams_index_;

    std::map<std::string, std::shared_ptr<DeviceInfo>> video_devices_;
    std::map<std::string, std::shared_ptr<DeviceInfo>> audio_devices_;
    uint64_t next_stream_id_{0};

    std::map<std::string, StreamStatus> stream_status_;

    void addStream(std::map<std::string, std::set<std::string>>& device_streams,
                   const std::string& device_id, const std::string& stream_id);

    void addStreamIndex(StreamSettings& settings);

    void removeStreamIndex(const StreamSettings& settings);

    void removeStreamIndex(std::map<GstDevice*, std::set<std::string>>& stream_index,
                           GstDevice* device_id, const std::string& stream_id);

    bool isActive(StreamSettings& settings);

    ConfigManager() = default;
public:
    static ConfigManager& getInstance();
    void load();
    void getStreams(std::map<std::string, StreamSettings>& streams);
    bool getStream(StreamSettings& stream, const std::string& id);
    std::string addStream(StreamSettings& stream);
    bool updateStream(const StreamSettings& settings);
    bool removeStream(const std::string& id);

    // to get enabeld streams and ouputs only
    void getActiveStreams(std::map<std::string, StreamSettings>& streams);

    // device api
    void addVideoDevice(const std::string& id, const std::string& name, GstDevice* device);
    void addAudioDevice(const std::string& id, const std::string& name, GstDevice* device);
    void removeVideoDevice(const std::string& id);
    void removeAudioDevice(const std::string& id);
    void getVideoDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& video_devices);
    void getAudioDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& audip_devices);
    GstDevice* getVideoDevice(const std::string& id);
    GstDevice* getAudioDevice(const std::string& id);

    // status api
    void updateStreamStatus(const std::string& id, const StreamStatus& stream_status);
    void getStreamsStatus(std::map<std::string, StreamStatus>& stream_status);
    bool getStreamStatus(StreamStatus& stream_status, const std::string& id);
};