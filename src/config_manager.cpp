#include <fstream>
#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <json_serialization.h>
#include <iostream>

static const gchar* get_device_id(const GstStructure* props) {
    const gchar* id = gst_structure_get_string(props, "device.id");
    if (!id) {
        id = gst_structure_get_string(props, "device.path");
    }
    return id;
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::start() {
    if (thread_) {
        return false;
    }

    thread_ = std::make_unique<std::thread>(std::ref(*this));
    if (!thread_) {
        return false;
    }

    return true;
}

void ConfigManager::operator()() {
    GstDeviceMonitor* dev_monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(dev_monitor, "Video/Source", NULL);
    gst_device_monitor_add_filter(dev_monitor, "Audio/Source", NULL);

    GstBus* dev_monitor_bus = gst_device_monitor_get_bus(dev_monitor);
    if (!gst_device_monitor_start(dev_monitor)) {
        return;
    }

    while (!exit_) {
        GstMessage* dev_monitor_msg = gst_bus_timed_pop(dev_monitor_bus, 200 * GST_MSECOND);
        if (!dev_monitor_msg) {
            continue;
        }

        GstDevice* device = nullptr;
        gchar* name = nullptr;
        gchar* klass = nullptr;
        const gchar* id = nullptr;

        switch (GST_MESSAGE_TYPE(dev_monitor_msg)) {
        case GST_MESSAGE_DEVICE_ADDED:
        {
            gst_message_parse_device_added(dev_monitor_msg, &device);
            GstStructure* props = gst_device_get_properties(device);
            if (props) {
                klass = gst_device_get_device_class(device);
                if (!strcmp(klass, "Source/Video")) {
                    id = get_device_id(props);
                    name = gst_device_get_display_name(device);
                    if (id && name) {
                        printf("video device added: %s id=%s\n", name, id);
                        addVideoDevice(id, name, device);
                    }
                }
                else if (!strcmp(klass, "Audio/Source")) {
                    id = get_device_id(props);
                    name = gst_device_get_display_name(device);
                    if (id && name) {
                        printf("audio device added: %s id=%s\n", name, id);
                        addAudioDevice(id, name, device);
                    }
                }

                gst_structure_free(props);
                g_free(name);
            }

            gst_object_unref(device);
            break;
        }

        case GST_MESSAGE_DEVICE_REMOVED:
        {
            gst_message_parse_device_removed(dev_monitor_msg, &device);
            GstStructure* props = gst_device_get_properties(device);
            if (props) {
                klass = gst_device_get_device_class(device);
                if (!strcmp(klass, "Source/Video")) {
                    id = get_device_id(props);
                    name = gst_device_get_display_name(device);
                    if (id && name) {
                        printf("video device removed: %s id=%s\n", name, id);
                        removeVideoDevice(id);
                    }
                }
                else if (!strcmp(klass, "Audio/Source")) {
                    id = get_device_id(props);
                    name = gst_device_get_display_name(device);
                    if (id && name) {
                        printf("audio device removed: %s id=%s\n", name, id);
                        removeAudioDevice(id);
                    }
                }
            }

            g_free(name);
            gst_object_unref(device);
            break;
        }

        default:
            break;
        }

        gst_message_unref(dev_monitor_msg);
    }

    gst_device_monitor_stop(dev_monitor);
    gst_object_unref(dev_monitor_bus);
}

void ConfigManager::stop() {
    if (thread_ && thread_->joinable()) {
        exit_ = true;
        thread_->join();
    }
}

void ConfigManager::setPreviewListener(PreviewUpdateListener* listener) {
    preview_listener_ = listener;
}

void ConfigManager::load() {
    std::ifstream config_if("conf/config.json");

    nlohmann::json config;
    config_if >> config;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (!config.contains("streams")) {
        streams_.clear();
        return;
    }

    // replace device ids with indexes
    for (auto& s_json : config["streams"]) {
        if (s_json.contains("video") && s_json["video"].contains("device")) {
            const std::string dev_id = s_json["video"]["device"].get<std::string>();
            const uint64_t device_index = ensureDeviceIndex(dev_id);
            s_json["video"]["device"] = device_index;
        }
    }

    {
        std::map<std::string, StreamSettings> streams;
        config.at("streams").get_to(streams);

        for (auto& [ _, stream ] : streams_) {
            addStreamIndex(stream);
        }

        streams_ = std::move(streams);
        next_stream_id_ = streams_.size();
    }
}

void ConfigManager::getStreams(std::map<std::string, StreamSettings>& streams) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& it : streams_) {
        streams[it.first] = it.second;
    }
}

void ConfigManager::getActiveStreams(std::map<std::string, StreamSettings>& streams) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    for (const auto& [id, src_settings] : streams_) {

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
            streams[id] = settings;
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

bool ConfigManager::addStream(StreamSettings& settings) {
    settings.id = std::to_string(next_stream_id_++);

    std::unique_lock<std::shared_mutex> lock(mutex_);
    addStreamIndex(settings);

    streams_[settings.id] = settings;
    return true;
}

bool ConfigManager::updateStream(StreamSettings& settings) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = streams_.find(settings.id);
    if (it == streams_.end()) {
        return false;
    }

    auto& old_settings = it->second;

    removeStreamIndex(old_settings);
    
    addStreamIndex(settings);
    return true;
}


void ConfigManager::addStreamIndex(StreamSettings& settings) {
    if (!settings.isEnabled()) {
        return;
    }

    if (settings.video) {
        const auto it = video_devices_.find(settings.video->device_index);
        if (it != video_devices_.end()) {
            video_streams_index_[it->second->device].insert(settings.id);
            settings.video->device = it->second->device;
        }
    }

    if (settings.audio) {
        const auto it = audio_devices_.find(settings.audio->device_index);
        if (it != audio_devices_.end()) {
            audio_streams_index_[it->second->device].insert(settings.id);
            settings.audio->device = it->second->device;
        }
    }
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

    removeStreamIndex(it->second);

    streams_.erase(it);
    return true;
}

uint64_t ConfigManager::ensureDeviceIndex(const std::string& device_id) {
    const uint64_t device_index = getDeviceIndex(device_id);
    if (device_index != 0) {
        return device_index;
    }

    auto it = device_to_index_.find(device_id);
    if (it != device_to_index_.end()) {
        return it->second;
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

void ConfigManager::addVideoDevice(const gchar* device_id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<VideoCaptureInfo>(name, device, preview_listener_, previews_version_);
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto device_index = ensureDeviceIndex(device_id);
    if (device_index == 0) {
        return;
    }

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second;
        if (settings.video && settings.video->device_index == device_index) {
            video_streams_index_[device].insert(it.first);
            settings.video->device = device;
        }
    }

    video_devices_[device_index] = std::move(di);
    previews_version_.fetch_add(1, std::memory_order_release);
}

void ConfigManager::addAudioDevice(const gchar* device_id, const std::string& name, GstDevice* device) {
    auto di = std::make_shared<AudioCaptureInfo>(name, device, preview_listener_, previews_version_);
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto device_index = ensureDeviceIndex(device_id);
    if (device_index == 0) {
        return;
    }

    // update index
    for (auto& it : streams_) {
        auto& settings = it.second;
        if (settings.audio && settings.audio->device_index == device_index) {
            audio_streams_index_[device].insert(it.first);
            settings.audio->device = device;
        }
    }

    audio_devices_[device_index] = std::move(di);
    previews_version_.fetch_add(1, std::memory_order_release);
}

void ConfigManager::removeVideoDevice(const gchar* device_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto device_index = getDeviceIndex(device_id);
    if (device_index == 0) {
        return;
    }

    auto vsi_it = video_streams_index_.find(video_devices_[device_index]->device);
    if (vsi_it != video_streams_index_.end()) {
        auto& streams = vsi_it->second;
        for (auto& it : streams) {
            auto& settings = streams_[it];
            if (settings.video) {
                settings.video->device = 0;
            }
        }
        video_streams_index_.erase(vsi_it);
    }
    video_devices_.erase(device_index);
    previews_version_.fetch_add(1, std::memory_order_release);
}

void ConfigManager::removeAudioDevice(const gchar* device_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto device_index = getDeviceIndex(device_id);
    if (device_index == 0) {
        return;
    }

    auto asi_it = audio_streams_index_.find(audio_devices_[device_index]->device);
    if (asi_it != audio_streams_index_.end()) {
        auto& streams = asi_it->second;
        for (auto& it : streams) {
            auto& settings = streams_[it];
            if (settings.audio) {
                settings.audio->device = 0;
            }
        }
        audio_streams_index_.erase(asi_it);
    }
    audio_devices_.erase(device_index);
    previews_version_.fetch_add(1, std::memory_order_release);
}

void ConfigManager::getVideoDevices(std::map<uint64_t, std::string>& video_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& it : video_devices_) {
        video_devices[it.first] = it.second->name;
    }
}
void ConfigManager::getAudioDevices(std::map<uint64_t, std::string>& audio_devices) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& it : audio_devices_) {
        audio_devices[it.first] = it.second->name;
    }
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

std::shared_ptr<VideoPreviewBuffer> ConfigManager::getVideoPreviewBuffer(const uint64_t cam_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = video_devices_.find(cam_id);
    if (it != video_devices_.end()) {
        return it->second->buffer;
    }

    return std::shared_ptr<VideoPreviewBuffer>();
}

std::shared_ptr<AudioPreviewBuffer> ConfigManager::getAudioPreviewBuffer(const uint64_t cam_id) {
    return std::shared_ptr<AudioPreviewBuffer>();
}

bool ConfigManager::getActivePreviews(std::map<GstDevice*, std::shared_ptr<VideoPreviewBuffer>>& video_previews,
                                      std::map<GstDevice*, std::shared_ptr<AudioPreviewBuffer>>& audio_previews,
                                      uint64_t& last_previews_version) {
    const uint64_t current_version = previews_version_.load(std::memory_order_acquire);
    if (current_version == last_previews_version) {
        return false;
    }

    last_previews_version = current_version;

    std::shared_lock<std::shared_mutex> lock(mutex_);

    for (auto& it : video_devices_) {
        auto& buffer = it.second->buffer;
        if (buffer->isPreviewEnabled()) {
            video_previews[it.second->device] = buffer;
        }
    }

    for (auto& it : audio_devices_) {
        auto& buffer = it.second->buffer;
        if (buffer->isPreviewEnabled()) {
            audio_previews[it.second->device] = buffer;
        }
    }

    return true;
}
