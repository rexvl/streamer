#include <gst/gst.h>
#include <gst/gstpipeline.h>
#include <media_pipeline.h>
#include <media_stream.h>
#include <media_utils.h>

MediaPipeline::MediaPipeline() {
}

MediaPipeline::~MediaPipeline() {
    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_element_get_state(pipeline_, NULL, NULL, GST_CLOCK_TIME_NONE);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

bool MediaPipeline::create() {
    pipeline_ = gst_pipeline_new(nullptr);
    if (!pipeline_) {
        return false;
    }

    bus_ = gst_element_get_bus(GST_ELEMENT(pipeline_));
    if (!bus_) {
        return false;
    }

    auto status = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);
    if (status == GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    return true;
}

GstElement* MediaPipeline::getElement() {
    return pipeline_;
}

GstElement* MediaPipeline::createChildBin() {
    return ::createChildBin(pipeline_);
}

std::shared_ptr<MediaCapture> MediaPipeline::ensureVideoCapture(GstDevice* device) {
    auto it = video_captures_.find(device);
    if (it != video_captures_.end()) {
        auto video = it->second.lock();
        if (video) {
            return video;
        }
    }

    auto video = std::make_shared<MediaCapture>(this);
    if (!video) {
        return nullptr;
    }

    if (!video->create(device)) {
        return nullptr;
    }

    video_captures_[device] = video;
    return video;
}

std::shared_ptr<MediaCapture> MediaPipeline::ensureAudioCapture(GstDevice* device) {
    auto it = audio_captures_.find(device);
    if (it != audio_captures_.end()) {
        auto audio = it->second.lock();
        if (audio) {
            return audio;
        }
    }

    auto audio = std::make_shared<MediaCapture>(this);
    if (!audio) {
        return nullptr;
    }

    if (!audio->create(device)) {
        return nullptr;
    }

    audio_captures_[device] = audio;
    return audio;
}

std::shared_ptr<MediaCapture> MediaPipeline::getAudioCapture(GstDevice* device) {
    return ensureAudioCapture(device);
}


bool MediaPipeline::ProcessMessage() {
    GstMessage* msg = gst_bus_timed_pop_filtered(bus_, 50 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
    if (!msg) {
        return true;
    }

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
    {
        GstObject* obj = GST_MESSAGE_SRC(msg);
        if (GST_IS_ELEMENT(obj)) {
            GstElement* element = GST_ELEMENT(obj);

            printf("%p\n", element);

            auto it = streams_.begin();
            while (it != streams_.end()) {
                auto& stream = it->second;
                if (gst_object_has_as_ancestor(GST_OBJECT(element), GST_OBJECT(stream->getElement()))) {
                    if (!stream->onError(element)) {
                        it = streams_.erase(it);
                    }
                    return true;
                }
                ++it;
            }
        }

        return false;
    }

    case GST_MESSAGE_EOS:
        return false;

    case GST_MESSAGE_STATE_CHANGED:
    {
        GstState old_state;
        GstState new_state;
        GstState pending;

        gst_message_parse_state_changed(
            msg,
            &old_state,
            &new_state,
            &pending);

        GstObject* obj = GST_MESSAGE_SRC(msg);
/*
        g_print("%s (%s): %s -> %s (pending: %s)\n",
            GST_OBJECT_NAME(obj),
            G_OBJECT_TYPE_NAME(obj),
            gst_element_state_get_name(old_state),
            gst_element_state_get_name(new_state),
            gst_element_state_get_name(pending));
*/

        if (obj == GST_OBJECT(pipeline_)) {
            if (state_ == GST_STATE_PLAYING && new_state != GST_STATE_PLAYING) {
                return false;
            }

            state_ = new_state;
        }
        break;
    }

    default:
        break;
    }

    return true;
}

bool MediaPipeline::IsPlaying() {
    return state_ == GST_STATE_PLAYING;
}

bool MediaPipeline::syncStreams(std::map<std::string, StreamSettings>& new_streams) {
    // sync existed streams
    auto cs_it = streams_.begin();
    while (cs_it != streams_.end()) {
        auto ns_it = new_streams.find(cs_it->first);
        if (ns_it == new_streams.end()) {
            // remove whole stream
            cs_it = streams_.erase(cs_it);
            continue;
        }

        const auto& settings = ns_it->second;
        auto& media_stream = cs_it->second;

        if (!media_stream->update(settings)) {
            cs_it = streams_.erase(cs_it);
            continue;
        }

        new_streams.erase(ns_it);
        cs_it++;
    }

    // create new streams
    for (const auto& [id, settings] : new_streams) {
        auto media_stream = std::make_unique<MediaStream>(this);
        if (!media_stream->create(settings)) {
            continue;
        }

        printf("created stream=%s\n", id.c_str());
        streams_[id] = std::move(media_stream);
    }

    // clear captures
    for (auto it = video_captures_.begin(); it != video_captures_.end(); ) {
        if (it->second.expired()) {
            it = video_captures_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = audio_captures_.begin(); it != audio_captures_.end(); ) {
        if (it->second.expired()) {
            it = audio_captures_.erase(it);
        } else {
            ++it;
        }
    }

    return true;
}

void MediaPipeline::syncVideoPreviews(std::map<GstDevice*, std::shared_ptr<VideoPreviewBuffer>>& active_previews) {
    auto it = video_previews_.begin();
    while (it != video_previews_.end()) {
        auto ap_it = active_previews.find(it->first);
        if (ap_it == active_previews.end()) {
            it = video_previews_.erase(it);
        } else {
            it++;
        }
    }

    for (auto& [device, buffer] : active_previews) {
        auto vp_it = video_previews_.find(device);
        if (vp_it != video_previews_.end()) {
            continue; // already started
        }

        auto video_preview = std::make_shared<VideoPreview>(this);
        if (!video_preview->create(device, buffer)) {
            continue;
        }

        video_previews_[device] = video_preview;
    }
}

void MediaPipeline::syncAudioPreviews(std::map<GstDevice*, std::shared_ptr<AudioPreviewBuffer>>& active_previews) {
    auto it = audio_previews_.begin();
    while (it != audio_previews_.end()) {
        auto ap_it = active_previews.find(it->first);
        if (ap_it == active_previews.end()) {
            it = audio_previews_.erase(it);
        } else {
            it++;
        }
    }

    for (auto& [device, buffer] : active_previews) {
        auto ap_it = audio_previews_.find(device);
        if (ap_it != audio_previews_.end()) {
            continue; // already started
        }

        auto audio_preview = std::make_shared<AudioPreview>(this);
        if (!audio_preview->create(device, buffer)) {
            continue;
        }

        audio_previews_[device] = audio_preview;
    }
}
