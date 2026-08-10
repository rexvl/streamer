#pragma once
#include <map>
#include <string>
#include <atomic>
#include <shared_mutex>
#include <config_manager.h>

class StreamStateStore {
    std::shared_mutex mutex_;
    std::map<std::string, std::atomic<double>> audio_levels_;
public:
    StreamStateStore() = default;
    void sync(const std::map<std::string, StreamSettings>& streams);
    void setAudioLevel(const std::string& id, double level);
    double getAudioLevel(const std::string& id);
};
