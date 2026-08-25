#pragma once
#include <list>
#include <nlohmann/json.hpp>
#include <config_manager.h>

inline
void from_json(const nlohmann::json& j, VideoSettings::Codec& c) {
    std::string s = j.get<std::string>();

    if (s == "x264enc") {
        c = VideoSettings::Codec::x264enc;
    } else if (s == "nvautogpuh264enc") {
        c = VideoSettings::Codec::nvautogpuh264enc;
    } else if (s == "qsvh264enc") {
        c = VideoSettings::Codec::qsvh264enc;
    } else
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
    j.at("device").get_to(c.device_id);

    c.width = j.value("width", 0);
    c.height = j.value("height", 0);
    c.fps_n = j.value("fps_n", 0);
    c.fps_d = j.value("fps_d", 1);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

inline
void from_json(const nlohmann::json& j, AudioSettings& c) {
    j.at("device").get_to(c.device_id);

    c.sampleRate = j.value("sampleRate", 0);
    c.channel_count = j.value("channels", 0);

    j.at("codec").get_to(c.codec);
    c.bitrate = j.value("bitrate", 0);
}

inline
void from_json(const nlohmann::json& j, OutputSettings& c) {
    if (j.contains("enabled")) {
        j.at("enabled").get_to(c.enabled);
    }

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
void from_json(const nlohmann::json& j, std::list<StreamSettings>& streams)
{
    streams.clear();

    uint32_t stream_id = 0;
    for (const auto& item : j)
    {
        StreamSettings stream;
        item.get_to(stream);
        stream.id = std::to_string(stream_id++);
        streams.emplace_back(stream);
    }
}

inline
void to_json(nlohmann::json& j, const VideoSettings::Codec& c) {
    if (c == VideoSettings::Codec::x264enc)
        j = "x264enc";
    else if (c == VideoSettings::Codec::nvautogpuh264enc) 
        j = "nvautogpuh264enc";
    else if (c == VideoSettings::Codec::qsvh264enc)
        j = "qsvh264enc";
    else
        throw std::runtime_error("Unknown video codec");
}

inline
void to_json(nlohmann::json& j, const VideoSettings& s) {
    j = nlohmann::json{
        { "device", s.device_id }
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
        { "device", s.device_id }
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
void to_json(nlohmann::json& j, const OutputSettings& s) {
    j = nlohmann::json{
        {"url", s.url},
        {"enabled", s.enabled}
    };
}

inline void to_json(nlohmann::json& j, const std::map<std::string, OutputSettings>& outputs) {
    j = nlohmann::json::array();

    for (const auto& it : outputs) {
        nlohmann::json out = it.second;
        // include map key as explicit id so clients can match runtime status
        out["id"] = it.first;
        j.push_back(out);
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

inline
void to_json(nlohmann::json& j, const SourceStatus status) {
    if (status == SourceStatus::kDisabled)
        j = "disabled";
    else if (status == SourceStatus::kUnknown)
        j = "unknown";
    else if (status == SourceStatus::kUnavailable) 
        j = "unavailable";
    else if (status == SourceStatus::kSuccess)
        j = "success";
    else if (status == SourceStatus::kFail)
        j = "fail";
    else
        throw std::runtime_error("Unknown source status");
}

inline
void to_json(nlohmann::json& j, const OutputStatus status) {
    if (status == OutputStatus::kUnknown)
        j = "unknown";
    else if (status == OutputStatus::kSuccess)
        j = "success";
    else if (status == OutputStatus::kFail)
        j = "fail";
    else
        throw std::runtime_error("Unknown source status");
}

inline
void to_json(nlohmann::json& j, const  std::map<std::string, OutputStatus>& output_status) {
    j = nlohmann::json::array();

    for (const auto& it : output_status) {
        nlohmann::json di;
        di["id"] = it.first;

        di["status"] = it.second;

        j.push_back(di);
    }
}

inline
void to_json(nlohmann::json& j, std::shared_ptr<StreamStatus>& status) {
    const auto video_status = status->getVideoStatus();
    if (video_status != SourceStatus::kUnknown) {
        j["video_status"] = video_status;
    }

    const auto audio_status = status->getAudioStatus();
    if (audio_status != SourceStatus::kUnknown) {
        j["audio_status"] = audio_status;
    }

    const auto output_status = status->getOutputStatus();
    j["outputs"] = output_status;
}


inline
void to_json(nlohmann::json& j, std::map<std::string, std::shared_ptr<StreamStatus>>& streams_status) {
    j = nlohmann::json::array();

    for (auto& it : streams_status) {
        if (it.second) {
            nlohmann::json item;
            item["id"] = it.first;
            to_json(item, it.second);
            j.push_back(std::move(item));
        }
    }
}
