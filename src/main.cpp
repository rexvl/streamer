#include <memory>
#include <string>
#include <deque>

#include <chrono>

#include <iostream>

#include <gst/gst.h>
#include <gst/rtsp/gstrtsptransport.h>

#include <config_manager.h>
#include <video_source.h>
#include <audio_source.h>
#include <media_output.h>
#include <media_stream.h>
#include <http_server.h>
#include <stream_state_store.h>

#include <windows.h> // SetConsoleOutputCP

static const gchar* get_device_id(const GstStructure* props) {
    const gchar* id = gst_structure_get_string(props, "device.id");
    if (!id) {
        id = gst_structure_get_string(props, "device.path");
    }

    return id;
}

void update_output_status(std::map<std::string, std::unique_ptr<MediaStream>>& cur_streams) {
    for (auto& cs_it : cur_streams) {
        auto& stream = cs_it.second;

        bool update = false;

        StreamStatus status;
        if (stream->video) {
            uint32_t frame_count = stream->video->frame_count.exchange(0, std::memory_order_relaxed);
            if (frame_count > 10) {
                status.video_status = SourceStatus::kSuccess;
                update = true;
            }
        }

        if (stream->audio) {
            uint32_t frame_count = stream->audio->frame_count.exchange(0, std::memory_order_relaxed);
            if (frame_count > 10) {
                status.audio_status = SourceStatus::kSuccess;
                update = true;
            }
        }

        for (auto& outputs_it : stream->outputs) {
            auto& output = outputs_it.second;
            uint32_t packet_count = output->packet_count.exchange(0, std::memory_order_relaxed);
            if (packet_count > 10) {
                status.output_status[outputs_it.first] = OutputStatus::kSuccess;
                update = true;
            }
        }

        if (update) {
            ConfigManager::getInstance().updateStreamStatus(cs_it.first, status);
        }
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    ConfigManager::getInstance().load();

    gst_init(nullptr, nullptr);

    StreamStateStore stream_states;

    HttpServer http_server(&stream_states);
    http_server.start();

    GstDeviceMonitor* dev_monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(dev_monitor, "Video/Source", NULL);
    gst_device_monitor_add_filter(dev_monitor, "Audio/Source", NULL);

    GstBus* dev_monitor_bus = gst_device_monitor_get_bus(dev_monitor);
    if (!gst_device_monitor_start(dev_monitor)) {
        return -1;
    }

    std::chrono::steady_clock::time_point last_sync = std::chrono::steady_clock::now();

    std::map<std::string, std::unique_ptr<MediaStream>> cur_streams;

    bool running = true;
    while (running) {
        GstMessage* dev_monitor_msg = gst_bus_timed_pop(dev_monitor_bus, 10 * GST_MSECOND);
        if (dev_monitor_msg) {
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
                            ConfigManager::getInstance().addVideoDevice(id, name, device);
                        }
                    } else if (!strcmp(klass, "Audio/Source")) {
                        id = get_device_id(props);
                        name = gst_device_get_display_name(device);
                        if (id && name) {
                            printf("audio device added: %s id=%s\n", name, id);
                            ConfigManager::getInstance().addAudioDevice(id, name, device);
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
                            ConfigManager::getInstance().removeVideoDevice(id);
                        }
                    } else if (!strcmp(klass, "Audio/Source")) { 
                        id = get_device_id(props);
                        name = gst_device_get_display_name(device);
                        if (id && name) {
                            printf("audio device removed: %s id=%s\n", name, id);
                            ConfigManager::getInstance().removeAudioDevice(id);
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

        std::set<std::string> video_previes;
        stream_states.getStartedPreviews(video_previes);

        auto cs_it = cur_streams.begin();
        while (cs_it != cur_streams.end()) {
            auto& stream = cs_it->second;
            if (video_previes.end() == video_previes.find(cs_it->first)) {
                stream->stopVideoPreview();
            } else {
                stream->startVideoPreview();
            }

            if (!stream->ProcessMessage()) {
                auto status = stream->getStatus();
                ConfigManager::getInstance().updateStreamStatus(cs_it->first, status);
                cs_it = cur_streams.erase(cs_it);
                continue;
            }
            cs_it++;
        }

        const auto cur_time = std::chrono::steady_clock::now();
        if (last_sync + std::chrono::milliseconds(2000) < cur_time) {
            last_sync = cur_time;

            std::map<std::string, StreamSettings> new_streams;
            ConfigManager::getInstance().getActiveStreams(new_streams);

            // sync existed streams
            auto cs_it = cur_streams.begin();
            while (cs_it != cur_streams.end()) { 
                auto ns_it = new_streams.find(cs_it->first);
                if (ns_it == new_streams.end()) {
                    // remove whole stream
                    cs_it = cur_streams.erase(cs_it);
                    continue;
                }

                const auto& settings = ns_it->second;
                auto& media_stream = cs_it->second;

                if (!media_stream->update(settings)) {
                    cs_it = cur_streams.erase(cs_it);
                    continue;
                }

                new_streams.erase(ns_it);
                cs_it++;
            }

            // create new streams
            for (const auto& ns_it : new_streams) {
                const auto& settings = ns_it.second;

                auto media_stream = std::make_unique<MediaStream>(ns_it.first, &stream_states);
                if (!media_stream->create(settings)) {
                    continue;
                }

                if (!media_stream->start()) {
                    auto status = media_stream->getStatus();
                    ConfigManager::getInstance().updateStreamStatus(ns_it.first, status);
                    continue;
                }

                printf("started stream=%s\n", ns_it.first.c_str());
                cur_streams[ns_it.first] = std::move(media_stream);
            }

            update_output_status(cur_streams);

            // remove orphans
            stream_states.sync(new_streams);
        }
    }

    http_server.stop();

    gst_device_monitor_stop(dev_monitor);

    gst_object_unref(dev_monitor_bus);

    return 0;
}