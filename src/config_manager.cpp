#include <fstream>
#include <nlohmann/json.hpp>
#include <config_manager.h>

void from_json(const nlohmann::json& j, VideoSettings::Codec& c) {
    std::string s = j.get<std::string>();

    if (s == "avc")
        c = VideoSettings::Codec::AVC;
    else if (s == "hevc")
        c = VideoSettings::Codec::HEVC;
    else
        throw std::runtime_error("Unknown output type: " + s);
}

void from_json(const nlohmann::json& j, AudioSettings::Codec& c) {
    std::string s = j.get<std::string>();

    if (s == "aac")
        c = AudioSettings::Codec::AAC;
    else
        throw std::runtime_error("Unknown output type: " + s);
}

void from_json(const nlohmann::json& j, VideoSettings& c) {
    j.at("device").get_to(c.device);

    c.width = j.value("whidth", 0);
    c.height = j.value("height", 0);
    c.fps_n = j.value("fps_n", 0);
    c.fps_d = j.value("fps_d", 1);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

void from_json(const nlohmann::json& j, AudioSettings& c) {
    j.at("device").get_to(c.device);

    c.sampleRate = j.value("sampleRate", 0);
    c.channel_count = j.value("channels", 0);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

void from_json(const nlohmann::json& j, OutputSettings::Type& type) {
    std::string s = j.get<std::string>();

    if (s == "rtmp")
        type = OutputSettings::Type::RTMP;
    else if (s == "rtsp")
        type = OutputSettings::Type::RTSP;
    else
        throw std::runtime_error("Unknown output type: " + s);
}

void from_json(const nlohmann::json& j, OutputSettings& c) {
    j.at("type").get_to(c.type);
    j.at("url").get_to(c.url);
}

void from_json(const nlohmann::json& j, StreamSettings& c) {

    if (j.contains("video")) {
        c.video = std::make_shared<VideoSettings>();
        j.at("video").get_to(*c.video);
    }

    if (j.contains("audio")) {
        c.audio = std::make_shared<AudioSettings>();
        j.at("audio").get_to(*c.audio);
    }

    j.at("outputs").get_to(c.outputs);
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::load() {
    std::ifstream config_if("C:/_CODE/streamer/conf/config.json");

    nlohmann::json config;
    config_if >> config;

    if (!config.contains("streams")) {
        streams_.clear();
        return;
    }

    config.at("streams").get_to(streams_);
}

void ConfigManager::getStreams(std::map<std::string, StreamSettings>& streams) {
    streams = streams_;
}