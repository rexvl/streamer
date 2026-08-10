#pragma once

#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>

class StreamStateStore;

struct lws_context;
struct lws;

struct lws_sorted_usec_list;
typedef struct lws_sorted_usec_list lws_sorted_usec_list_t;

class HttpServer {
    // libwebsockets context pointer (opaque)
    StreamStateStore* stream_states_;

    lws_context* context_{nullptr};
    std::thread thread_;
public:
    bool running_ = false;

    // public for callbacks
    HttpServer(StreamStateStore* stream_states) : 
        stream_states_(stream_states) {
    }

    static void state_check_timer_cb(lws_sorted_usec_list_t* sul);

    void start();
    void stop();
};
