#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <atomic>
#include <vector>
#include <shared_mutex>
#include <config_manager.h>

struct VideoPreview {
    std::vector<uint8_t> data_;
    uint64_t preview_index_;
    VideoPreview(const uint8_t* buffer, const size_t buffer_size, const uint64_t preview_index) :
        data_(buffer, buffer + buffer_size),
        preview_index_(preview_index) {
    }
};

class StreamStateStore {
    std::shared_mutex mutex_;
    std::map<std::string, std::atomic<double>> audio_levels_;
    std::map<std::string, std::shared_ptr<VideoPreview>> video_previews_;
    std::set<std::string> started_video_previews_;
public:
    StreamStateStore() = default;
    void sync(const std::map<std::string, StreamSettings>& streams);
    void setAudioLevel(const std::string& id, double level);
    double getAudioLevel(const std::string& id);
    void setVideoPreview(const std::string& id, std::shared_ptr<VideoPreview>& preview);
    std::shared_ptr<VideoPreview> getVideoPreview(const std::string& id);
    void startVideoPreview(const std::string& stream_id);
    void stopVideoPreview(const std::string& stream_id);
    void getStartedPreviews(std::set<std::string>& video_previews);
};
