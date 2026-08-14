#pragma once

#include <thread>
#include <vector>
#include <deque>
#include <condition_variable>
#include <memory>

#include <libwebsockets.h>
#include <stream_state_store.h>

class HttpServer {
    std::vector<unsigned char> buffer_;

    StreamStateStore* stream_states_;

    lws_context* context_{nullptr};
    std::thread thread_;

    struct AudioLevelWebsocket {
        std::string id_;
        std::atomic<double> level_{ 0.0 };

        bool update(double new_level) {
            double old_level = level_.load(std::memory_order_relaxed);

            while (std::abs(old_level - new_level) >= 10.0) {
                if (level_.compare_exchange_weak(
                    old_level,
                    new_level,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                    return true;
                }
            }

            return false;
        }

        AudioLevelWebsocket(const std::string& id) :
            id_(id) {
        }
    };

    std::map<lws*, std::unique_ptr<AudioLevelWebsocket>> ws_audio_clients_;

    struct VideoPreviewWebsocket {
        const std::string id_;
        std::shared_ptr<VideoPreview> video_preview_;
        uint64_t sent_item_index_{ 0 };

        bool update(const std::shared_ptr<VideoPreview>& video_preview) {
            if (video_preview &&
                (!video_preview_ ||
                    video_preview_->preview_index_ != video_preview->preview_index_)) {
                video_preview_ = video_preview;
                return true;
            }

            return false;
        }

        VideoPreviewWebsocket(const std::string& id) :
            id_(id) {
        }
    };

    std::map<lws*, std::unique_ptr<VideoPreviewWebsocket>> ws_video_clients_;

    std::map<std::string, int> video_previews_;

    lws_sorted_usec_list_t sul_{ };
    bool timer_started_{ false };

    std::atomic<bool> running_ = false;

    static int callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    static int callback_ws_video(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

    static void state_check_timer_cb(lws_sorted_usec_list_t* sul);

    void startTimer();
    void onTimer();

    void addVideoPreview(struct lws* wsi, const std::string& stream_id);
    void removeVideoPreview(struct lws* wsi);
public:
    HttpServer(StreamStateStore* stream_states) : 
        buffer_(1024 * 1024),
        stream_states_(stream_states) {
    }

    void start();
    void stop();
};
