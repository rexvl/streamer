#pragma once
#include <nlohmann/json.hpp>
#include <config_manager.h>

inline
void from_json(const nlohmann::json& j, VideoSettings::Codec& c) {
    std::string s = j.get<std::string>();

    if (s == "avc")
        c = VideoSettings::Codec::AVC;
    else if (s == "hevc")
        c = VideoSettings::Codec::HEVC;
    else
        throw std::runtime_error("Unknown video codec: " + s);
}

inline
void from_json(const nlohmann::json& j, AudioSettings::Codec& c) {
    std::string s = j.get<std::string>();

    if (s == "aac")
        c = AudioSettings::Codec::AAC;
    else
        throw std::runtime_error("Unknown audio codec: " + s);
}

inline
void from_json(const nlohmann::json& j, VideoSettings& c) {
    j.at("device").get_to(c.device);

    c.width = j.value("width", 0);
    c.height = j.value("height", 0);
    c.fps_n = j.value("fps_n", 0);
    c.fps_d = j.value("fps_d", 1);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

inline
void from_json(const nlohmann::json& j, AudioSettings& c) {
    j.at("device").get_to(c.device);

    c.sampleRate = j.value("sampleRate", 0);
    c.channel_count = j.value("channels", 0);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

inline
void from_json(const nlohmann::json& j, OutputSettings::Type& type) {
    std::string s = j.get<std::string>();

    if (s == "rtmp")
        type = OutputSettings::Type::RTMP;
    else if (s == "rtsp")
        type = OutputSettings::Type::RTSP;
    else
        throw std::runtime_error("Unknown output type: " + s);
}

inline
void from_json(const nlohmann::json& j, OutputSettings& c) {
    j.at("type").get_to(c.type);
    j.at("url").get_to(c.url);
}

inline
void from_json(const nlohmann::json& j, StreamSettings& c) {
    if (j.contains("video")) {
        c.video = std::make_shared<VideoSettings>();
        j.at("video").get_to(*c.video);
    }

    if (j.contains("audio")) {
        c.audio = std::make_shared<AudioSettings>();
        j.at("audio").get_to(*c.audio);
    }

    uint32_t output_id = 0;
    if (j.contains("outputs")) {
        for (const auto& output : j["outputs"])
        {
            OutputSettings o;
            output.get_to(o);
            o.id = std::to_string(output_id++);

            c.outputs[o.id] = o;
        }
    }
}

inline
void from_json(const nlohmann::json& j, std::map<std::string, StreamSettings>& streams)
{
    streams.clear();

    uint32_t stream_id = 0;
    for (const auto& item : j)
    {
        StreamSettings stream;
        item.get_to(stream);
        stream.id = std::to_string(stream_id++);

        streams[stream.id] = stream;
    }
}

inline
void to_json(nlohmann::json& j, const VideoSettings::Codec& c) {
    if (c == VideoSettings::Codec::AVC)
        j = "avc";
    else if (c == VideoSettings::Codec::HEVC) 
        j = "hevc";
    else
        throw std::runtime_error("Unknown video codec");
}

inline
void to_json(nlohmann::json& j, const VideoSettings& s) {
    j = nlohmann::json{
        { "device", s.device }
    };

    if (s.width > 0) {
        j["width"] = s.width;
    }

    if (s.height > 0) {
        j["height"] = s.height;
    }

    if (s.fps_n > 0 && s.fps_d > 0) {
        j["fps_n"] = s.fps_n;
        j["fps_d"] = s.fps_d;
    }

    j["codec"] = s.codec;

    if (s.bitrate > 0) {
        j["bitrate"] = s.bitrate;
    }
}

inline
void to_json(nlohmann::json& j, const AudioSettings::Codec& c) {
    if (c == AudioSettings::Codec::AAC)
        j = "aac";
    else
        throw std::runtime_error("Unknown audio codec");
}

inline
void to_json(nlohmann::json& j, const AudioSettings& s) {
    j = nlohmann::json{
        { "device", s.device }
    };

    if (s.sampleRate > 0) {
        j["sampleRate"] = s.sampleRate;
    }

    if (s.channel_count > 0) {
        j["channels"] = s.channel_count;
    }

    j["codec"] = s.codec;

    if (s.bitrate > 0) {
        j["bitrate"] = s.bitrate;
    }
}

inline
void to_json(nlohmann::json& j, const OutputSettings::Type& t) {
    if (t == OutputSettings::Type::RTMP)
        j = "rtmp";
    else if (t == OutputSettings::Type::RTSP)
        j = "rtsp";
    else
        throw std::runtime_error("Unknown output type");
}

inline
void to_json(nlohmann::json& j, const OutputSettings& s) {
    j = nlohmann::json{
        {"type", s.type},
        {"url", s.url}
    };
}

inline void to_json(nlohmann::json& j, const std::map<std::string, OutputSettings>& outputs) {
    j = nlohmann::json::array();

    for (const auto& it : outputs) {
        j.push_back(it.second);
    }
}


inline
void to_json(nlohmann::json& j, const StreamSettings& s) {
    j = nlohmann::json{};

    j["id"] = s.id;

    if (s.video) {
        j["video"] = *s.video;
    }

    if (s.audio) {
        j["audio"] = *s.audio;
    }

    j["outputs"] = s.outputs;
}

inline void to_json(nlohmann::json& j, const std::map<std::string, StreamSettings>& streams) {
    j = nlohmann::json::array();

    for (const auto& it : streams) {
        j.push_back(it.second);
    }
}

inline
void to_json(nlohmann::json& j, const DeviceInfo& s) {
    j = nlohmann::json{
        { "name", s.name_}
    };
}

inline
void to_json(nlohmann::json& j, const std::map<std::string, std::shared_ptr<DeviceInfo>>& devices) {
    // Return devices as an array of objects { "id": <device_id>, "name": <display_name> }
    // so web UI can display human-readable names while keeping ids.
    j = nlohmann::json::array();

    for (const auto& it : devices) {
        nlohmann::json di;
        di["id"] = it.first;
        di["name"] = it.second ? it.second->name_ : std::string();
        j.push_back(di);
    }
}