#include <fstream>
#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <json_serialization.h>
#include <iostream>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::setPreviewListener(PreviewUpdateListener* listener) {
    preview_listener_ = listener;
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

    // replace device ids with indexes
    for (auto& s_json : config["streams"]) {
        if (s_json.contains("video") && s_json["video"].contains("device")) {
            const std::string dev_id = s_json["video"]["device"].get<std::string>();
            uint64_t index = ensureDeviceIndex(dev_id); 
            s_json["video"]["device"] = index;
        }
    }

    {
        std::list<StreamSettings> settings;
        config.at("streams").get_to(settings);

        std::map<std::string, StreamContext> streams;
        for (const auto& it : settings) {
            StreamContext context(it, preview_listener_);
            streams[it.id]   = std::move(context);
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [ _, stream ] : streams) {
            addStreamIndex(stream);
        }

        streams_ = std::move(streams);
        next_stream_id_ = streams_.size();
    }
}

void ConfigManager::getStreams(std::map<std::string, StreamSettings>& streams) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& it : streams_) {
        streams[it.first] = it.second.settings;
    }
}

void ConfigManager::getActiveStreams(std::map<std::string, StreamContext>& streams) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    for (auto& it : streams_) {
        auto& src_context  = it.second;
        auto& src_settings = src_context.settings;

        StreamSettings settings;
        if (src_settings.video && src_settings.video->device) {
            settings.video = src_settings.video;
        }

        if (src_settings.audio && src_settings.audio->device) {
            settings.audio = src_settings.audio;
        }

        if (!settings.video && !settings.audio) {
            continue;
        }

        for (auto& it : src_settings.outputs) {
            const auto& output = it.second;
            if (output.enabled) {
                settings.outputs[it.first] = output;
            }
        }

        if (!settings.outputs.empty()) {
            StreamContext context;
            context.settings = std::move(settings);
            context.preview  = src_context.preview;
            context.status   = src_context.status;
            streams[it.first] = std::move(context);
        }
    }
}

bool ConfigManager::getStream(StreamSettings& stream, const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(id);
    if (it != streams_.end()) {
        stream = it->second.settings;
        return true;
    }
    return false;
}

bool ConfigManager::addStream(StreamSettings& settings) {
    settings.id = std::to_string(next_stream_id_++);
    StreamContext context(settings, preview_listener_);

    std::unique_lock<std::shared_mutex> lock(mutex_);
    addStreamIndex(context);

    streams_[settings.id] = context;
    return true;
}

bool ConfigManager::updateStream(const StreamSettings& settings) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(settings.id);
    if (it == streams_.end()) {
        return false;
    }

    auto& context = it->second;

    removeStreamIndex(context.settings);

    context.settings = std::move(settings);
    
    addStreamIndex(context);
    return true;
}


void ConfigManager::addStreamIndex(StreamContext& context) {
    auto& settings = context.settings;

    if (!settings.isEnabled()) {
        return;
    }

    if (settings.video) {
        const auto it = video_devices_.find(settings.video->device_index);
        if (it != video_devices_.end()) {
            video_streams_index_[it->second->device_].insert(settings.id);
            settings.video->device = it->second->device_;
        }
    }

    if (settings.audio) {
        const auto it = audio_devices_.find(settings.audio->device_index);
        if (it != audio_devices_.end()) {
            audio_streams_index_[it->second->device_].insert(settings.id);
            settings.audio->device = it->second->device_;
        }
    }

    auto video_status = SourceStatus::kDisabled;
    if (settings.video) {
        video_status = (settings.video->device) ? SourceStatus::kSuccess : SourceStatus::kUnavailable;
    }

    auto audio_status = SourceStatus::kDisabled;
    if (settings.audio) {
        audio_status = (settings.audio->device) ? SourceStatus::kSuccess : SourceStatus::kUnavailable;
    }

    auto& status = context.status;
    status->setVideoStatus(video_status);
    status->setAudioStatus(audio_status);
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

    removeStreamIndex(it->second.settings);

    streams_.erase(it);
    return true;
}

void ConfigManager::addVideoDevice(const std::string& device_id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const uint64_t device_index = ensureDeviceIndex(device_id);

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second.settings;
        if (settings.video && settings.video->device_index == device_index) {
            video_streams_index_[device].insert(it.first);
            settings.video->device = device;
        }
    }

    video_devices_[device_index] = std::move(di);
}

void ConfigManager::addAudioDevice(const std::string& id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const uint64_t device_index = ensureDeviceIndex(id);

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second.settings;
        if (settings.audio && settings.audio->device_index == device_index) {
            audio_streams_index_[device].insert(it.first);
            settings.audio->device = device;
            it.second.status->setAudioStatus(SourceStatus::kSuccess);
        }
    }

    audio_devices_[device_index] = std::move(di);
}

void ConfigManager::removeVideoDevice(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const uint64_t device_index = getDeviceIndex(id);

    auto vsi_it = video_streams_index_.find(video_devices_[device_index]->device_);
    if (vsi_it != video_streams_index_.end()) {
        auto& streams = vsi_it->second;
        for (auto& it : streams) {
            auto& context = streams_[it];
            if (context.settings.video) {
                context.settings.video->device = 0;
            }
            context.status->setVideoStatus(SourceStatus::kUnavailable);
        }
        video_streams_index_.erase(vsi_it);
    }
    video_devices_.erase(device_index);
}

void ConfigManager::removeAudioDevice(const std::string& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const uint64_t device_index = getDeviceIndex(id);

    auto asi_it = audio_streams_index_.find(audio_devices_[device_index]->device_);
    if (asi_it != audio_streams_index_.end()) {
        auto& streams = asi_it->second;
        for (auto& it : streams) {
            auto& context = streams_[it];
            if (context.settings.audio) {
                context.settings.audio->device = 0;
            }
            context.status->setAudioStatus(SourceStatus::kUnavailable);
        }
        audio_streams_index_.erase(asi_it);
    }
    audio_devices_.erase(device_index);
}

void ConfigManager::getVideoDevices(std::map<uint64_t, std::shared_ptr<DeviceInfo>>& video_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    video_devices = video_devices_;
}
void ConfigManager::getAudioDevices(std::map<uint64_t, std::shared_ptr<DeviceInfo>>& audio_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    audio_devices = audio_devices_;
}
/*
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
*/
std::shared_ptr<PreviewState> ConfigManager::getPreviewState(const std::string& stream_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return it->second.preview;
    }

    return nullptr;
}

void ConfigManager::getStreamsStatus(std::map<std::string, std::shared_ptr<StreamStatus>>& stream_status) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& it : streams_) {
        stream_status[it.first] = it.second.status;
    }
}

std::shared_ptr<StreamStatus> ConfigManager::getStreamStatus(const std::string& stream_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return it->second.status;
    }

    return nullptr;
}

uint64_t ConfigManager::ensureDeviceIndex(const std::string& device_id) {
    uint64_t device_index = getDeviceIndex(device_id);
    if (device_index != 0) {
        return device_index;
    }

    last_device_index_++;
    device_to_index_[device_id] = last_device_index_;
    index_to_device_[last_device_index_] = device_id;
    return last_device_index_;

}

uint64_t ConfigManager::getDeviceIndex(const std::string& device_id) {
    auto it = device_to_index_.find(device_id);
    if (it != device_to_index_.end()) {
        return it->second;
    }

    return 0;
}

std::string ConfigManager::getDeviceId(const uint64_t index) {
    auto it = index_to_device_.find(index);
    if (it != index_to_device_.end()) {
        return it->second;
    }

    return std::string();
}
