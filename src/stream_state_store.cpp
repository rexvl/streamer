#include <stream_state_store.h>

void StreamStateStore::sync(const std::map<std::string, StreamSettings>& streams) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = audio_levels_.begin();
    while (it != audio_levels_.end()) {
        if (streams.end() == streams.find(it->first)) {
            it = audio_levels_.erase(it);
        } else {
            it++;
        }
    }
}

void StreamStateStore::setAudioLevel(const std::string& id, double level) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    audio_levels_[id].store(level, std::memory_order_release);
}

double StreamStateStore::getAudioLevel(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = audio_levels_.find(id);
    if (it != audio_levels_.end()) {
        return it->second.load(std::memory_order_relaxed);
    }

    return 0.0;
}

void StreamStateStore::setVideoPreview(const std::string& id, std::shared_ptr<VideoPreview>& preview) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    video_previews_[id] = preview;
}

std::shared_ptr<VideoPreview> StreamStateStore::getVideoPreview(const std::string& id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = video_previews_.find(id);
    if (it != video_previews_.end()) {
        return it->second;
    }

    return std::shared_ptr<VideoPreview>();
}


void StreamStateStore::startVideoPreview(const std::string& stream_id) {
    printf("start viedeo preview: stream=%s\n", stream_id.c_str());
    std::unique_lock<std::shared_mutex> lock(mutex_);
    started_video_previews_.insert(stream_id);
}

void StreamStateStore::stopVideoPreview(const std::string& stream_id) {
    printf("stop viedeo preview: stream=%s\n", stream_id.c_str());
    std::unique_lock<std::shared_mutex> lock(mutex_);
    started_video_previews_.erase(stream_id);
}

void StreamStateStore::getStartedPreviews(std::set<std::string>& video_previews) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    video_previews = started_video_previews_;
}
