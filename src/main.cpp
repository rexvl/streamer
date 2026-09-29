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

#include <media_pipeline.h>

#include <windows.h> // SetConsoleOutputCP

static const gchar* get_device_id(const GstStructure* props) {
    const gchar* id = gst_structure_get_string(props, "device.id");
    if (!id) {
        id = gst_structure_get_string(props, "device.path");
    }

    return id;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    HttpServer http_server;
    ConfigManager::getInstance().setPreviewListener(&http_server);

    ConfigManager::getInstance().load();

    http_server.start();

    gst_init(nullptr, nullptr);

    GstDeviceMonitor* dev_monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(dev_monitor, "Video/Source", NULL);
    gst_device_monitor_add_filter(dev_monitor, "Audio/Source", NULL);

    GstBus* dev_monitor_bus = gst_device_monitor_get_bus(dev_monitor);
    if (!gst_device_monitor_start(dev_monitor)) {
        return -1;
    }

    std::chrono::steady_clock::time_point last_sync = std::chrono::steady_clock::now();

    std::shared_ptr<MediaPipeline> pipeline;

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

        if (!pipeline) {
            pipeline = std::make_unique<MediaPipeline>();
            if (!pipeline || !pipeline->create()) {
                return -1;
            }
        }

        if (!pipeline->ProcessMessage()) {
            pipeline.reset();
            continue;
        }

        if (!pipeline->IsPlaying()) {
            continue;
        }

        //printf("playing...\n");

        const auto cur_time = std::chrono::steady_clock::now();
        if (last_sync + std::chrono::milliseconds(2000) < cur_time) {
            last_sync = cur_time;

            std::map<std::string, StreamContext> new_streams;
            ConfigManager::getInstance().getActiveStreams(new_streams);


            pipeline->syncStreams(new_streams);
        }
    }

    http_server.stop();

    gst_device_monitor_stop(dev_monitor);

    gst_object_unref(dev_monitor_bus);

    return 0;
}