#pragma once
#include <string>
#include <map>
#include <mutex>

#include <gst/gst.h>

struct VideoSettings {
    std::string device;

    int width{ 0 };
    int height{ 0 };
    int fps_n{ 0 };
    int fps_d{ 1 };

    enum class Codec { AVC, HEVC };
    Codec codec;

    int bitrate{ 0 };

    bool operator != (const VideoSettings& other) {
        return false;
    }
};

struct AudioSettings {
    std::string device;

    int sampleRate{ 0 };
    int channel_count{ 0 };

    enum class Codec { AAC };
    Codec codec;

    int bitrate{ 0 };

    bool operator != (const AudioSettings& other) {
        return false;
    }
};

struct OutputSettings {
    std::string id;
    enum class Type { RTSP, RTMP };
    Type type;
    std::string url;
};

struct StreamSettings {
    std::string id;
    std::shared_ptr<VideoSettings>  video;
    std::shared_ptr<AudioSettings>  audio;
    std::map<std::string, OutputSettings> outputs;
};

struct DeviceInfo {
    std::string name_;
    GstDevice* device_{ nullptr };

    DeviceInfo(const std::string& name, GstDevice* device) :
        name_(name), device_(device) {
    }
};

class ConfigManager {
    std::mutex mutex_;
    std::map<std::string, StreamSettings> streams_;
    std::map<std::string, std::shared_ptr<DeviceInfo>> video_devices_;
    std::map<std::string, std::shared_ptr<DeviceInfo>> audio_devices_;
    uint64_t next_stream_id_{0};

    ConfigManager() = default;
public:
    static ConfigManager& getInstance();
    void load();
    void getStreams(std::map<std::string, StreamSettings>& streams);
    bool getStream(StreamSettings& stream, const std::string& id);
    std::string addStream(StreamSettings& stream);
    bool removeStream(const std::string& id);

    // device api
    void addVideoDevice(const std::string& id, const std::string& name, GstDevice* device);
    void addAudioDevice(const std::string& id, const std::string& name, GstDevice* device);
    void removeVideoDevice(const std::string& id);
    void removeAudioDevice(const std::string& id);
    void getVideoDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& video_devices);
    void getAudioDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& audip_devices);
    GstDevice* getVideoDevice(const std::string& id);
    GstDevice* getAudioDevice(const std::string& id);
};