#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <http_server.h>
#include <json_serialization.h>

#include <libwebsockets.h>
#include <fstream>
#include <sstream>
#include <iostream>

#include <stream_state_store.h>

static std::string mime_type_for_path(const std::string& path) {
    if (path.size() >= 5 && path.rfind(".html") == path.size() - 5) return "text/html";
    if (path.size() >= 3 && path.rfind(".js") == path.size() - 3) return "application/javascript";
    if (path.size() >= 4 && path.rfind(".css") == path.size() - 4) return "text/css";
    if (path.size() >= 4 && path.rfind(".png") == path.size() - 4) return "image/png";
    if (path.size() >= 4 && path.rfind(".svg") == path.size() - 4) return "image/svg+xml";
    return "application/octet-stream";
}

static void send_http_response(struct lws* wsi, const std::string& body, const std::string& content_type, int status = 200) {
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << status << " OK\r\n";
    hdr << "Content-Type: " << content_type << "\r\n";
    hdr << "Content-Length: " << body.size() << "\r\n";
    hdr << "Connection: close\r\n";
    hdr << "\r\n";

    const std::string resp = hdr.str() + body;
    size_t n = resp.size();
    unsigned char* out = (unsigned char*)malloc(LWS_PRE + n);
    if (!out) return;
    memcpy(out + LWS_PRE, resp.data(), n);
    lws_write(wsi, out + LWS_PRE, (int)n, LWS_WRITE_HTTP);
    free(out);
}

template<typename T>
static std::string json_to_string(const T& data) {
    nlohmann::json j = data;
    return j.dump(4);
}

enum class HttpMethod {
    POST, PUT
};

// forward declaration of protocols array so other functions can reference
// per-HTTP-connection/session data
struct http_session {
    HttpMethod method;
    std::string path;
    std::string body;
};

// HTTP callback: serve API endpoints and static files from ./html
int HttpServer::callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    switch (reason) {
    case LWS_CALLBACK_HTTP: 
    {
        char buf[1024];

        // Determine method and path using available URI tokens
        if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) > 0) {
            std::string path = std::string(buf);

            if (path == "/streams") {
                // respond with list for GET or default
                std::map<std::string, StreamSettings> streams;
                ConfigManager::getInstance().getStreams(streams);
                send_http_response(wsi, json_to_string(streams), "application/json");
                return 0;
            }

            if (path.rfind("/streams/", 0) == 0) {
                std::string id = path.substr(strlen("/streams/"));

                StreamSettings settings;
                if (!ConfigManager::getInstance().getStream(settings, id)) {
                    send_http_response(wsi, json_to_string(nlohmann::json{ {"error","Stream not found"} }), "application/json", 404);
                    return 0;
                }
                send_http_response(wsi, json_to_string(settings), "application/json");
                return 0;
            }

            if (path == "/status") {
                std::map<std::string, StreamStatus> streams_status;
                ConfigManager::getInstance().getStreamsStatus(streams_status);
                send_http_response(wsi, json_to_string(streams_status), "application/json");
                return 0;
            }

            if (path.rfind("/status/", 0) == 0) {
                std::string id = path.substr(strlen("/status/"));
                StreamStatus streams_status;
                if (!ConfigManager::getInstance().getStreamStatus(streams_status, id)) {
                    send_http_response(wsi, json_to_string(nlohmann::json{ {"error","Stream not found"} }), "application/json", 404);
                    return 0;
                }
                send_http_response(wsi, json_to_string(streams_status), "application/json");
                return 0;
            }

            if (path == "/devices/video") {
                std::map<std::string, std::shared_ptr<DeviceInfo>> video_devices;
                ConfigManager::getInstance().getVideoDevices(video_devices);
                send_http_response(wsi, json_to_string(video_devices), "application/json");
                return 0;
            }

            if (path == "/devices/audio") {
                std::map<std::string, std::shared_ptr<DeviceInfo>> audio_devices;
                ConfigManager::getInstance().getAudioDevices(audio_devices);
                send_http_response(wsi, json_to_string(audio_devices), "application/json");
                return 0;
            }

            if (path.empty() || path == "/") {
                path = "/index.html";
            }

            std::string file_path = std::string("./html") + path;
            std::ifstream ifs(file_path, std::ios::binary);
            if (!ifs.good()) {
                send_http_response(wsi, "Not found", "text/plain", 404);
                return 0;
            }

            std::ostringstream ss;
            ss << ifs.rdbuf();
            const std::string body = ss.str();
            const std::string ct = mime_type_for_path(file_path);
            send_http_response(wsi, body, ct);
            return 0;
        }

        if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_POST_URI) > 0) {
            http_session* sesion = new http_session();
            sesion->method = HttpMethod::POST;
            sesion->path = std::string(buf);
            lws_set_wsi_user(wsi, sesion);
            return 0;
        }

        if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_PUT_URI) > 0) {
            http_session* sesion = new http_session();
            sesion->method = HttpMethod::PUT;
            sesion->path = std::string(buf);
            lws_set_wsi_user(wsi, sesion);
            return 0;
        }

        if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_DELETE_URI) > 0) {
            std::string path = std::string(buf);

            if (path.rfind("/streams/", 0) == 0) {
                std::string id = path.substr(strlen("/streams/"));

                if (!ConfigManager::getInstance().removeStream(id)) {
                    send_http_response(wsi, json_to_string(nlohmann::json{ {"error","Stream not found"} }), "application/json", 404);
                    return 0;
                }

                send_http_response(wsi, json_to_string(nlohmann::json{ {"id", id}, {"status","deleted"} }), "application/json");
                return 0;
            }
        }

        send_http_response(wsi, "Not found", "text/plain", 404);
        return 0;
    }

    case LWS_CALLBACK_HTTP_BODY: {
        // receive a chunk of the request body
        auto session = (http_session*)lws_wsi_user(wsi);
        if (in && len > 0) {
            session->body.append((const char*)in, len);
        }
        return 0;
    }

    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
        // full body received; act based on saved path/method
        auto session = (http_session*)lws_wsi_user(wsi);
        const std::string& path = session->path;

        try {
            if (session->method == HttpMethod::POST && path == "/streams") {
                auto j = nlohmann::json::parse(session->body);
                StreamSettings settings = j.get<StreamSettings>();

                std::string id = ConfigManager::getInstance().addStream(settings);
                send_http_response(wsi, json_to_string(nlohmann::json{{"id", id}}), "application/json", 201);
                return 0;
            }

            if (session->method == HttpMethod::PUT && path.rfind("/streams/", 0) == 0) {
                std::string id = path.substr(strlen("/streams/"));
                auto j = nlohmann::json::parse(session->body);

                StreamSettings settings = j.get<StreamSettings>();
                settings.id = id;

                if (!ConfigManager::getInstance().updateStream(settings)) {
                    send_http_response(wsi, json_to_string(nlohmann::json{{"error","Stream not found"}}), "application/json", 404);
                    return 0;
                }
                send_http_response(wsi, json_to_string(nlohmann::json{{"id", id}, {"status","updated"}}), "application/json");
                return 0;
            }
        } catch (const std::exception& e) {
            send_http_response(wsi, json_to_string(nlohmann::json{{"error", e.what()}}), "application/json", 400);
            return 0;
        }

        // fallback
        send_http_response(wsi, "Not found", "text/plain", 404);
        return 0;
    }

    case LWS_CALLBACK_CLOSED_HTTP: 
    {
        auto session = (http_session*)lws_wsi_user(wsi);
        if (session) {
            delete session;
            lws_set_wsi_user(wsi, nullptr);
        }
        return 0;
    }

    default:
        break;
    }
    return 0;
}

int HttpServer::callback_ws_video(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    auto instansce = static_cast<HttpServer*>(lws_get_protocol(wsi)->user);
    if (!instansce) {
        return -1;
    }

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        char buf[256];
        if (!lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI)) {
            return -1;
        }

        const auto path = std::string(buf);
        if (path.rfind("/ws-video/", 0) != 0) {
            return -1;
        }

        const auto id = path.substr(strlen("/ws-video/"));
        if (id.empty()) {
            return -1;
        }

        instansce->addVideoPreview(wsi, id);
        break;
    }
    case LWS_CALLBACK_CLOSED: {
        instansce->removeVideoPreview(wsi);
        break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        // send video_preview if available
        auto it = instansce->ws_video_clients_.find(wsi);
        if (it == instansce->ws_video_clients_.end()) {
            break;
        }

        auto& ws = it->second;
        auto video_preview = ws->video_preview_;

        if (video_preview->preview_index_ == ws->sent_item_index_) {
            break; // skip if already sent
        }

        ws->sent_item_index_ = video_preview->preview_index_;

        if (!video_preview || video_preview->data_.empty()) {
            break;
        }

        const auto& data = video_preview->data_;
        const size_t size = data.size();
        if (size > instansce->buffer_.size() - LWS_PRE) {
            return -1;
        }

        memcpy(&instansce->buffer_[LWS_PRE], data.data(), size);

        const int written = lws_write(wsi, &instansce->buffer_[LWS_PRE], size, LWS_WRITE_BINARY);
        if (written != size) {
            return -1;
        }
        break;
    }
    case LWS_CALLBACK_RECEIVE: {
        // ignore incoming data for now
        break;
    }
    default:
        break;
    }
    return 0;
}

int HttpServer::callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    auto instansce = static_cast<HttpServer*>(lws_get_protocol(wsi)->user);
    if (!instansce) {
        return -1;
    }

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        char buf[256];
        if (!lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI)) {
            return -1;
        }

        const auto path = std::string(buf);
        if (path.rfind("/ws-audio/", 0) != 0) {
            return -1;
        }

        const auto id = path.substr(strlen("/ws-audio/"));
        if (id.empty()) {
            return -1;
        }

        instansce->ws_audio_clients_.emplace(wsi, new AudioLevelWebsocket(id));

        if (!instansce->timer_started_) {
            printf("start timer\n");
            instansce->startTimer();
            instansce->timer_started_ = true;
        }
        break;
    }
    case LWS_CALLBACK_CLOSED: {
        instansce->ws_audio_clients_.erase(wsi);
        break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        // send one audio chunk if available
        auto it = instansce->ws_audio_clients_.find(wsi);
        if (it == instansce->ws_audio_clients_.end()) {
            break;
        }

        auto& ws = it->second;
        double level = ws->level_.load(std::memory_order_relaxed);

        std::string buffer = json_to_string(nlohmann::json{ {"level", level } });
        if (buffer.empty()) {
            return 0;
        }

        if (buffer.size() > instansce->buffer_.size() - LWS_PRE) {
            return -1;
        }

        memcpy(&instansce->buffer_[LWS_PRE], buffer.data(), buffer.size());

        const int written = lws_write(wsi, &instansce->buffer_[LWS_PRE], buffer.size(), LWS_WRITE_TEXT);
        if (written != buffer.size()) {
            return -1;
        }
        break;
    }
    case LWS_CALLBACK_RECEIVE: {
        // ignore incoming data for now
        break;
    }
    default:
        break;
    }
    return 0;
}

void HttpServer::startTimer() {
    lws_sul_schedule(context_, 0, &sul_, state_check_timer_cb, 50 * LWS_US_PER_MS);
}

void HttpServer::onTimer() {
    if (ws_audio_clients_.empty() && ws_video_clients_.empty()) {
        printf("stop timer\n");
        timer_started_ = false;
        return;
    }

    for (auto& it : ws_audio_clients_) {
        auto& ws = it.second;
        auto level = stream_states_->getAudioLevel(ws->id_);
        if (ws->update(level)) {
            lws_callback_on_writable(it.first);
        }
    }

    for (auto& it : ws_video_clients_) {
        auto& ws = it.second;
        auto video_preview = stream_states_->getVideoPreview(ws->id_);
        if (ws->update(video_preview)) {
            lws_callback_on_writable(it.first);
        }
    }

    startTimer();
}

void HttpServer::state_check_timer_cb(lws_sorted_usec_list_t* sul) {
    auto instance = lws_container_of(sul, HttpServer, sul_);
    if (instance) {
        instance->onTimer();
    }
}

void HttpServer::start() {
    thread_ = std::thread([this]() {
        lws_set_log_level(0, NULL); // disable console logging

        struct lws_context_creation_info info;
        memset(&info, 0, sizeof(info));

        struct lws_protocols protocols[] = {
            { "http", callback_http, 0, 0, 0, this },
            { "ws-audio", callback_ws_audio, 0, 0, 0, this },
            { "ws-video", callback_ws_video, 0, 0, 0, this },
            { nullptr, nullptr, 0, 0, 0, nullptr }
        };

        info.port = 8080;
        info.iface = nullptr;
        info.protocols = protocols;
        info.gid = -1;
        info.uid = -1;

        context_ = lws_create_context(&info);
        if (!context_) {
            std::cerr << "Failed to create libwebsockets context" << std::endl;
            return;
        }

        running_ = true;
        while (running_) {
            int ret = lws_service(context_, 1000);
            if (ret < 0) {
                break;
            }
        }

        lws_context_destroy(context_);
        context_ = nullptr;
    });
}

void HttpServer::stop() {
    running_ = false;

    if (context_) {
        lws_cancel_service(context_);
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

void HttpServer::addVideoPreview(struct lws* wsi, const std::string& stream_id) {
    auto it = video_previews_.find(stream_id);
    if (it == video_previews_.end()) {
        video_previews_[stream_id] = 1;
        stream_states_->startVideoPreview(stream_id);
    } else {
        it->second++;
    }

    ws_video_clients_.emplace(wsi, new VideoPreviewWebsocket(stream_id));

    if (!timer_started_) {
        printf("start timer\n");
        startTimer();
        timer_started_ = true;
    }
}

void HttpServer::removeVideoPreview(struct lws* wsi) {
    auto it = ws_video_clients_.find(wsi);
    if (it == ws_video_clients_.end()) {
        return;
    }

    auto& ws_info = it->second;
    auto vp_it = video_previews_.find(ws_info->id_);
    if (vp_it != video_previews_.end()) {
        vp_it->second--;
        if (vp_it->second <= 0) {
            video_previews_.erase(vp_it);
            stream_states_->stopVideoPreview(ws_info->id_);
        }
    }

    ws_video_clients_.erase(it);
}