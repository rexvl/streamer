#pragma once

#include <thread>
#include <vector>
#include <deque>
#include <condition_variable>
#include <memory>

#include <libwebsockets.h>
#include <preview_state.h>

class HttpServer {
    std::vector<unsigned char> buffer_;

    lws_context* context_{nullptr};
    std::thread thread_;

    struct AudioLevelWebsocket {
        std::shared_ptr<PreviewState> preview_;
        double level_{ 0.0 };

        bool update() {
            double new_level = preview_->getAudioLevel();
            if (std::abs(new_level - level_) >= 10.0) {
                level_ = new_level;
                return true;
            }

            return false;
        }

        AudioLevelWebsocket(const std::shared_ptr<PreviewState>& preview) :
            preview_(preview) {
        }
    };

    std::map<lws*, std::unique_ptr<AudioLevelWebsocket>> ws_audio_clients_;

    struct VideoPreviewWebsocket {
        std::shared_ptr<PreviewState> preview_state_;
        std::shared_ptr<const VideoPreview> video_preview_;
        uint64_t sent_item_index_{ 0 };

        bool update() {
            auto video_preview = preview_state_->getPreview();
            if (!video_preview) {
                return false;
            }

            if (!video_preview_ || video_preview_->preview_index_ != video_preview->preview_index_) {
                video_preview_ = video_preview;
                return true;
            }

            return false;
        }

        VideoPreviewWebsocket(const std::shared_ptr<PreviewState>& preview_state) :
            preview_state_(preview_state) {
            preview_state_->addVideoClient();
        }

        ~VideoPreviewWebsocket() {
            preview_state_->removeVideoClient();
        }
    };

    std::map<lws*, std::unique_ptr<VideoPreviewWebsocket>> ws_video_clients_;

    std::map<std::string, int> video_previews_;

    lws_sorted_usec_list_t sul_{ };
    bool timer_started_{ false };

    std::atomic<bool> running_ = false;

    std::mutex preview_mutex_;
    std::map<std::string, std::shared_ptr<PreviewState>> preview_states_;

    static int callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_video(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

    static void state_check_timer_cb(lws_sorted_usec_list_t* sul);

    void startTimer();
    void onTimer();

    bool addVideoPreviewClient(struct lws* wsi, const std::string& stream_id);
    void removeVideoPreviewClient(struct lws* wsi);

    std::shared_ptr<PreviewState> getPreviewState(const std::string& stream_id);
public:
    HttpServer() : 
        buffer_(1024 * 1024) {
    }

    void start();
    void stop();

    void addPreviewState(const std::string& stream_id, const std::shared_ptr<PreviewState>& preview_state);
    void removePreviewState(const std::string& stream_id);
};
