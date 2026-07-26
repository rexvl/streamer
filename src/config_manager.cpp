#include <fstream>
#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <json_serialization.h>

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::load() {
    std::ifstream config_if("conf/config.json");

    nlohmann::json config;
    config_if >> config;

    if (!config.contains("streams")) {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.clear();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config.at("streams").get_to(streams_);
        next_stream_id_ = streams_.size();
    }
}

void ConfigManager::getStreams(std::map<std::string, StreamSettings>& streams) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams = streams_;
}

bool ConfigManager::getStream(StreamSettings& stream, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(id);
    if (it != streams_.end()) {
        stream = it->second;
        return true;
    }
    return false;
}

std::string ConfigManager::addStream(StreamSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings.id = std::to_string(next_stream_id_++);
    streams_[settings.id] = settings;
    return settings.id;
}

bool ConfigManager::updateStream(const StreamSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(settings.id);
    if (it != streams_.end()) {
        it->second = settings;
        return true;
    }

    return false;
}

bool ConfigManager::removeStream(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(id);
    if (it != streams_.end()) {
        streams_.erase(it);
        return true;
    }

    return false;
}

void ConfigManager::addVideoDevice(const std::string& id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::lock_guard<std::mutex> lock(mutex_);
    video_devices_[id] = std::move(di);
}

void ConfigManager::addAudioDevice(const std::string& id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<DeviceInfo>(name, device);
    std::lock_guard<std::mutex> lock(mutex_);
    audio_devices_[id] = std::move(di);
}

void ConfigManager::removeVideoDevice(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    video_devices_.erase(id);
}

void ConfigManager::removeAudioDevice(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_devices_.erase(id);
}

void ConfigManager::getVideoDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& video_devices) {
    std::lock_guard<std::mutex> lock(mutex_);
    video_devices = video_devices_;
}
void ConfigManager::getAudioDevices(std::map<std::string, std::shared_ptr<DeviceInfo>>& audio_devices) {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_devices = audio_devices_;
}

GstDevice* ConfigManager::getVideoDevice(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = video_devices_.find(id);
    if (it != video_devices_.end()) {
        return it->second->device_;
    }
    return nullptr;
}

GstDevice* ConfigManager::getAudioDevice(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = audio_devices_.find(id);
    if (it != audio_devices_.end()) {
        return it->second->device_;
    }
    return nullptr;
}
