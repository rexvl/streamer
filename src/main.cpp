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

int main() {
    SetConsoleOutputCP(CP_UTF8);

    gst_init(nullptr, nullptr);

    HttpServer http_server;
    ConfigManager::getInstance().setPreviewListener(&http_server);

    ConfigManager::getInstance().load();

    ConfigManager::getInstance().start();


    http_server.start();

    std::chrono::steady_clock::time_point last_sync = std::chrono::steady_clock::now();
    uint64_t last_previews_version = 0;

    std::shared_ptr<MediaPipeline> pipeline;

    bool running = true;
    while (running) {
        if (!pipeline) {
            pipeline = std::make_unique<MediaPipeline>();
            if (!pipeline || !pipeline->create()) {
                return -1;
            }
            last_previews_version = 0;
        }

        if (!pipeline->ProcessMessage()) {
            pipeline.reset();
            continue;
        }

        if (!pipeline->IsPlaying()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::map<GstDevice*, std::shared_ptr<VideoPreviewBuffer>> video_previews;
        std::map<GstDevice*, std::shared_ptr<AudioPreviewBuffer>> audio_previews;
        if (ConfigManager::getInstance().getActivePreviews(video_previews, audio_previews, last_previews_version)) {
            pipeline->syncVideoPreviews(video_previews);
            pipeline->syncAudioPreviews(audio_previews);
        }

        const auto cur_time = std::chrono::steady_clock::now();
        if (last_sync + std::chrono::milliseconds(2000) < cur_time) {
            last_sync = cur_time;

            std::map<std::string, StreamSettings> new_streams;
            ConfigManager::getInstance().getActiveStreams(new_streams);

            pipeline->syncStreams(new_streams);
        }
    }

    http_server.stop();

    return 0;
}