#include <nlohmann/json.hpp>
#include <config_manager.h>
#include <http_server.h>
#include <json_serialization.h>

#include <libwebsockets.h>
#include <fstream>
#include <sstream>
#include <iostream>

static HttpServer* g_server = nullptr;

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

static int callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
static int callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

static struct lws_protocols protocols[] = {
    { "http", callback_http, 0, 0, 0, nullptr },
    { "ws-audio", callback_ws_audio, 0, 0, 0, nullptr },
    { nullptr, nullptr, 0, 0, 0, nullptr }
};

// HTTP callback: serve API endpoints and static files from ./html
static int callback_http(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
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

// WebSocket audio protocol
static std::vector<struct lws*> ws_clients;
static std::mutex ws_clients_mutex;

static int callback_ws_audio(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        std::lock_guard<std::mutex> lk(ws_clients_mutex);
        ws_clients.push_back(wsi);
        break;
    }
    case LWS_CALLBACK_CLOSED: {
        std::lock_guard<std::mutex> lk(ws_clients_mutex);
        ws_clients.erase(std::remove(ws_clients.begin(), ws_clients.end(), wsi), ws_clients.end());
        break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
        // send one audio chunk if available
        if (!g_server) break;
        std::shared_ptr<std::vector<uint8_t>> chunk;
        {
            std::unique_lock<std::mutex> lk(g_server->audio_mutex_);
            if (g_server->audio_queue_.empty()) break;
            chunk = g_server->audio_queue_.front();
            g_server->audio_queue_.pop_front();
        }

        if (chunk && !chunk->empty()) {
            size_t n = chunk->size();
            unsigned char* buf = (unsigned char*)malloc(LWS_PRE + n);
            if (!buf) break;
            memcpy(buf + LWS_PRE, chunk->data(), n);
            lws_write(wsi, buf + LWS_PRE, (int)n, LWS_WRITE_BINARY);
            free(buf);
        }

        // if there are more chunks, request another writable callback
        {
            std::unique_lock<std::mutex> lk(g_server->audio_mutex_);
            if (!g_server->audio_queue_.empty()) {
                lws_callback_on_writable(wsi);
            }
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

void HttpServer::start() {
    thread_ = std::thread([this]() {
        lws_set_log_level(0, NULL); // disable console logging

        g_server = this;

        struct lws_context_creation_info info;
        memset(&info, 0, sizeof(info));

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
            lws_service(context_, 50);
            // notify writable to all ws if we have audio
            std::unique_lock<std::mutex> lk(audio_mutex_);
            if (!audio_queue_.empty()) {
                // request writable for each client
                std::lock_guard<std::mutex> lk2(ws_clients_mutex);
                for (auto c : ws_clients) {
                    lws_callback_on_writable(c);
                }
            }
            lk.unlock();
            // sleep a little to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        lws_context_destroy(context_);
        context_ = nullptr;
    });
}

void HttpServer::stop() {
    running_ = false;
    if (context_) lws_cancel_service(context_);
    if (thread_.joinable()) thread_.join();
}

void HttpServer::pushAudioChunk(std::shared_ptr<std::vector<uint8_t>> chunk) {
    if (!chunk) return;
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        audio_queue_.push_back(chunk);
        // keep queue bounded (drop oldest)
        while (audio_queue_.size() > 200) audio_queue_.pop_front();
    }
    if (context_) {
        // wake service loop so it can schedule writeables
        lws_cancel_service(context_);
    }
}
