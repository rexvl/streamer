#pragma once
#include <string>
#include <map>

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
    enum class Type { RTSP, RTMP };
    Type type;
    std::string url;
};

struct StreamSettings {
    std::shared_ptr<VideoSettings>  video;
    std::shared_ptr<AudioSettings>  audio;
    std::map<std::string, OutputSettings> outputs;
};

class ConfigManager {
    std::map<std::string, StreamSettings> streams_;

    ConfigManager() = default;
public:
    static ConfigManager& getInstance();
    void load();
    void getStreams(std::map<std::string, StreamSettings>& streams);
};