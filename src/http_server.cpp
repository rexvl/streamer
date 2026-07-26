#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <http_server.h>
#include <json_serialization.h>

template<typename T>
static void set_content(httplib::Response& res, const T& data) {
    nlohmann::json j = data;
    const std::string data_json = j.dump(4);
    res.set_content(data_json, "application/json");
}

void HttpServer::start() {
    thread_ = std::thread([this]()
    {
        server_.Get("/status",
            [](const httplib::Request&, httplib::Response& res)
            {
                res.set_content("OK", "text/plain");
            });

        server_.Get("/streams",
            [](const httplib::Request&, httplib::Response& res)
            {
                std::map<std::string, StreamSettings> streams;
                ConfigManager::getInstance().getStreams(streams);

                set_content(res, streams);
            });

        server_.Get(R"(/streams/(\w+))",
            [](const httplib::Request& req, httplib::Response& res)
            {
                const std::string id = req.matches[1];

                StreamSettings stream;
                if (!ConfigManager::getInstance().getStream(stream, id)) {
                    res.status = 404;
                    set_content(res, nlohmann::json{
                        {"error", "Stream not found"}
                        });
                    return;
                }

                set_content(res, stream);
            });

        server_.Post("/streams",
            [](const httplib::Request& req, httplib::Response& res)
            {
                try {
                    auto j = nlohmann::json::parse(req.body);
                    StreamSettings settings = j.get<StreamSettings>();

                    std::string id = ConfigManager::getInstance().addStream(settings);

                    res.status = 201;

                    set_content(res, nlohmann::json{
                        {"id", id}
                        });
                } catch (const std::exception& e) {
                    res.status = 400;
                    set_content(res, nlohmann::json{
                        {"error", e.what()}
                        });
                }
            });

        server_.Delete(R"(/streams/(\w+))",
            [](const httplib::Request& req, httplib::Response& res)
            {
                std::string id = req.matches[1];

                if (!ConfigManager::getInstance().removeStream(id)) {
                    res.status = 404;
                    set_content(res, nlohmann::json{
                        {"error", "Stream not found"}
                        });
                    return;
                }

                set_content(res, nlohmann::json{
                    {"id", id}, 
                    {"status", "deleted"}
                    });
            });

        server_.Get("/devices/video",
            [](const httplib::Request&, httplib::Response& res)
            {
                std::map<std::string, std::shared_ptr<DeviceInfo>> video_devices;
                ConfigManager::getInstance().getVideoDevices(video_devices);

                set_content(res, video_devices);
            });

        server_.Get("/devices/audio",
            [](const httplib::Request&, httplib::Response& res)
            {
                std::map<std::string, std::shared_ptr<DeviceInfo>> audio_devices;
                ConfigManager::getInstance().getAudioDevices(audio_devices);

                set_content(res, audio_devices);
            });


        server_.listen("0.0.0.0", 8080);
    });
}

void HttpServer::stop() {
    server_.stop();

    if (thread_.joinable()) {
        thread_.join();
    }
}