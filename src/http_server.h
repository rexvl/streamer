#pragma once
#include <thread>
#include <httplib.h>

class HttpServer {
    httplib::Server server_;
    std::thread thread_;

public:
    HttpServer() = default;

    void start();
    void stop();
};
