#pragma once

#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>

struct lws_context;
struct lws;

class HttpServer {
    // libwebsockets context pointer (opaque)
    lws_context* context_{nullptr};
    std::thread thread_;
public:
    // audio queue for websocket streaming (accessible to internal callbacks)
    std::deque<std::shared_ptr<std::vector<uint8_t>>> audio_queue_;
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    bool running_ = false;

    // public for callbacks
    HttpServer() = default;

    void start();
    void stop();

    // call from other threads to push raw audio chunks to be sent to WS clients
    void pushAudioChunk(std::shared_ptr<std::vector<uint8_t>> chunk);
};
