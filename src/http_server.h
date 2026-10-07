#pragma once

#include <thread>
#include <vector>
#include <map>
#include <deque>
#include <condition_variable>
#include <memory>

#include <libwebsockets.h>
#include <preview_state.h>

class HttpServer : public PreviewUpdateListener {
    std::vector<unsigned char> buffer_;

    lws_context* context_{nullptr};
    std::thread thread_;

    struct AudioLevelWebsocket {
        std::shared_ptr<AudioPreviewBuffer> buffer_;
        double level_{ 0.0 };

        bool update() {
/*
            double new_level = preview_->getAudioLevel();
            if (std::abs(new_level - level_) >= 10.0) {
                level_ = new_level;
                return true;
            }
*/
            return false;
        }

        AudioLevelWebsocket(const std::shared_ptr<AudioPreviewBuffer>& buffer) :
            buffer_(buffer) {
            buffer_->addClient();
        }

        ~AudioLevelWebsocket() {
            buffer_->removeClient();
        }
    };

    std::map<lws*, std::unique_ptr<AudioLevelWebsocket>> ws_audio_clients_;

    struct VideoPreviewWebsocket {
        std::shared_ptr<VideoPreviewBuffer> buffer_;
        std::shared_ptr<const VideoPreviewFrame> video_preview_;
        uint64_t sent_item_index_{ 0 };

        bool update() {
            auto video_preview = buffer_->getPreview();
            if (!video_preview) {
                return false;
            }

            if (!video_preview_ || video_preview_->index() != video_preview->index()) {
                video_preview_ = video_preview;
                return true;
            }

            return false;
        }

        VideoPreviewWebsocket(const std::shared_ptr<VideoPreviewBuffer>& buffer) :
            buffer_(buffer) {
            buffer_->addClient();
        }

        ~VideoPreviewWebsocket() {
            buffer_->removeClient();
        }
    };

    std::map<lws*, std::unique_ptr<VideoPreviewWebsocket>> ws_video_clients_;

    std::map<std::string, int> video_previews_;

    lws_sorted_usec_list_t sul_{ };
    bool timer_started_{ false };

    std::atomic<bool> running_ = false;
/*
    std::mutex preview_mutex_;
    std::map<std::string, std::shared_ptr<PreviewState>> preview_states_;

    struct VideoPreviewInfo {
        std::shared_ptr<VideoPreview> video_preview;
        uint32_t clients_count{ 0 };
    };

    std::map<std::string, VideoPreviewInfo> video_previews_;
*/

    static int callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_video(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

    static void state_check_timer_cb(lws_sorted_usec_list_t* sul);

    bool addVideoPreviewClient(struct lws* wsi, const uint64_t id);
    void removeVideoPreviewClient(struct lws* wsi);
public:
    HttpServer() :
        buffer_(1024 * 1024) {
    }

    void start();
    void stop();

    void onPreviewUpdated() override;
};
