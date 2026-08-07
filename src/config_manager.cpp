#include <fstream>
#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <json_serialization.h>
#include <iostream>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::load() {
    std::ifstream config_if("conf/config.json");

    nlohmann::json config;
    config_if >> config;

    if (!config.contains("streams")) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        streams_.clear();
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        config.at("streams").get_to(streams_);

        for (auto& it : streams_) {
            addStreamIndex(it.second);
        }

        next_stream_id_ = streams_.size();
    }
}

void ConfigManager::getStreams(std::map<std::string, StreamSettings>& streams) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    streams = streams_;
}

void ConfigManager::getActiveStreams(std::map<std::string, StreamSettings>& streams) {
    getStreams(streams);

    auto it = streams.begin();
    while (it != streams.end()) {
        auto& settings = it->second;

        bool source_available = false;
        if (settings.video) {
            if (settings.video->device) {
                source_available = true;
            }
        }

        if (settings.audio) {
            if (settings.audio->device) {
                source_available = true;
            }
        }

        if (!source_available) {
            it  = streams.erase(it);
            continue;
        }

        // remove disabled outputs
        auto& outputs = settings.outputs;
        auto outputs_it = outputs.begin();
        while (outputs_it != outputs.end()) {
            if (!outputs_it->second.enabled) {
                outputs_it = outputs.erase(outputs_it);
            } else {
                outputs_it++;
            }
        }

        if (outputs.empty()) {
            it = streams.erase(it);
        } else {
            it++;
        }
    }
}

bool ConfigManager::getStream(StreamSettings& stream, const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(id);
    if (it != streams_.end()) {
        stream = it->second;
        return true;
    }
    return false;
}

std::string ConfigManager::addStream(StreamSettings& settings) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    settings.id = std::to_string(next_stream_id_++);

    addStreamIndex(settings);

    streams_[settings.id] = settings;
    // debug: log created stream outputs and enabled flags
    try {
        std::cout << "ConfigManager::addStream id=" << settings.id << " outputs=" << settings.outputs.size() << "\n";
        for (const auto &it : settings.outputs) {
            std::cout << "  out id=" << it.first << " enabled=" << (it.second.enabled ? "true" : "false") << " url=" << it.second.url << "\n";
        }
    } catch (...) {}

    return settings.id;
}

bool ConfigManager::updateStream(const StreamSettings& settings) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(settings.id);
    if (it == streams_.end()) {
        return false;
    }

    auto& cur_settings = it->second;
    // debug: log incoming update
    try {
        std::cout << "ConfigManager::updateStream id=" << settings.id << " outputs=" << settings.outputs.size() << "\n";
        for (const auto &it2 : settings.outputs) {
            std::cout << "  in out id=" << it2.first << " enabled=" << (it2.second.enabled ? "true" : "false") << " url=" << it2.second.url << "\n";
        }
    } catch (...) {}

    removeStreamIndex(cur_settings);

    cur_settings = settings;
    addStreamIndex(cur_settings);
    return true;
}


void ConfigManager::addStreamIndex(StreamSettings& settings) {
    if (!settings.isEnabled()) {
        return;
    }

    if (settings.video) {
        const auto it = video_devices_.find(settings.video->device_id);
        if (it != video_devices_.end()) {
            video_streams_index_[it->second->device_].insert(settings.id);
            settings.video->device = it->second->device_;
        }
    }

    if (settings.audio) {
        const auto it = audio_devices_.find(settings.audio->device_id);
        if (it != audio_devices_.end()) {
            audio_streams_index_[it->second->device_].insert(settings.id);
            settings.audio->device = it->second->device_;
        }
    }

    StreamStatus status;
    status.id = settings.id;
    status.video_status = SourceStatus::kDisabled;
    status.audio_status = SourceStatus::kDisabled;

    if (settings.video) {
        status.video_status = (settings.video->device) ? SourceStatus::kSuccess : SourceStatus::kUnavailable;
    }

    if (settings.audio) {
        status.audio_status = (settings.audio->device) ? SourceStatus::kSuccess : SourceStatus::kUnavailable;
    }

    stream_status_[settings.id] = status;
}

void ConfigManager::removeStreamIndex(const StreamSettings& settings) {
    if (!settings.isEnabled()) {
        return;
    }

    if (settings.video) {
        removeStreamIndex(video_streams_index_, settings.video->device, settings.id);
    }

    if (settings.audio) {
        removeStreamIndex(audio_streams_index_, settings.audio->device, settings.id);
    }
}

void ConfigManager::removeStreamIndex(std::map<GstDevice*, std::set<std::string>>& stream_index,
                                      GstDevice* device, const std::string& stream_id) {
    auto it = stream_index.find(device);
    if (it == stream_index.end()) {
        return;
    }

    it->second.erase(stream_id);

    if (it->second.empty()) {
        stream_index.erase(it);
    }
}

bool ConfigManager::removeStream(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(id);
    if (it == streams_.end()) {
        return false;
    }

    const auto& settings = it->second;
    removeStreamIndex(settings);

    streams_.erase(it);
    return true;
}

void ConfigManager::addVideoDevice(const std::string& id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second;
        if (settings.video && settings.video->device_id == id) {
            video_streams_index_[device].insert(it.first);
            settings.video->device = device;
            stream_status_[it.first].video_status = SourceStatus::kSuccess;
        }
    }

    video_devices_[id] = std::move(di);
}

void ConfigManager::addAudioDevice(const std::string& id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second;
        if (settings.audio && settings.audio->device_id == id) {
            audio_streams_index_[device].insert(it.first);
            settings.audio->device = device;
            stream_status_[it.first].audio_status = SourceStatus::kSuccess;
        }
    }

    audio_devices_[id] = std::move(di);
}

void ConfigManager::removeVideoDevice(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto vsi_it = video_streams_index_.find(video_devices_[id]->device_);
    if (vsi_it != video_streams_index_.end()) {
        auto& streams = vsi_it->second;
        for (auto& it : streams) {
            auto& stream = streams_[it];
            stream.video->device = 0;
            stream_status_[it].video_status = SourceStatus::kUnavailable;
        }
        video_streams_index_.erase(vsi_it);
    }
    video_devices_.erase(id);
}

void ConfigManager::removeAudioDevice(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto asi_it = audio_streams_index_.find(audio_devices_[id]->device_);
    if (asi_it != audio_streams_index_.end()) {
        auto& streams = asi_it->second;
        for (auto& it : streams) {
            auto& stream = streams_[it];
            stream.audio->device = 0;
            stream_status_[it].audio_status = SourceStatus::kUnavailable;
        }
        audio_streams_index_.erase(asi_it);
    }
    audio_devices_.erase(id);
}

void ConfigManager::getVideoDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& video_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    video_devices = video_devices_;
}
void ConfigManager::getAudioDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& audio_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    audio_devices = audio_devices_;
}

GstDevice* ConfigManager::getVideoDevice(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = video_devices_.find(id);
    if (it != video_devices_.end()) {
        return it->second->device_;
    }
    return nullptr;
}

GstDevice* ConfigManager::getAudioDevice(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = audio_devices_.find(id);
    if (it != audio_devices_.end()) {
        return it->second->device_;
    }
    return nullptr;
}

void ConfigManager::updateStreamStatus(const std::string& id, const StreamStatus& new_status) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = stream_status_.find(id);
    if (it == stream_status_.end()) {
        // skip for non-existing stream
        return;
    }

    auto& cur_status = it->second;
    if (new_status.video_status != SourceStatus::kUnknown) {
        cur_status.video_status = new_status.video_status;
    }

    if (new_status.audio_status != SourceStatus::kUnknown) {
        cur_status.audio_status = new_status.audio_status;
    }

    for (auto& it : new_status.output_status) {
        if (it.second != OutputStatus::kUnknown) {
            cur_status.output_status[it.first] = it.second;
        }
    }
}

void ConfigManager::getStreamsStatus(std::map<std::string, StreamStatus>& stream_status) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    stream_status = stream_status_;
}

bool ConfigManager::getStreamStatus(StreamStatus& status, const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = stream_status_.find(id);
    if (it == stream_status_.end()) {
        // skip for non-existing stream
        return false;
    }

    status = it->second;
    return true;
}