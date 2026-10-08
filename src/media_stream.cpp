#include <media_stream.h>

#include <video_source.h>
#include <audio_source.h>
#include <media_output.h>
#include <media_pipeline.h>
#include <media_utils.h>
#include <config_manager.h>

#include <gst/gst.h>
#include <cstdio>
#include <deque>

MediaStream::MediaStream(MediaPipeline* pipeline) :
    pipeline_(pipeline) {
    printf("MediaStream::MediaStream\n");
}

bool MediaStream::create(const StreamSettings& settings) {
    printf("MediaStream::create\n");

    bin_ = pipeline_->createChildBin();
    if (!bin_) {
        return false;
    }

    if (!gst_element_sync_state_with_parent(bin_)) {
        printf("MediaStream::create: failed to sync bin_ state\n");
        return false;
    }

    if (settings.video && settings.video->device) {
        if (!addVideo(*settings.video)) {
            return false;
        }
    }

    if (settings.audio && settings.audio->device) {
        if (!addAudio(*settings.audio)) {
            return false;
        }
    }

    if (!syncOutputs(settings.outputs)) {
        return false;
    }

    return true;
}

bool MediaStream::addVideo(const VideoSettings& settings) {
    if (video || !outputs.empty() || !settings.device) {
        return false;
    }

    auto video_capture = pipeline_->ensureVideoCapture(settings.device);
    if (!video_capture) {
        return false;
    }

    video = std::make_unique<VideoSource>(bin_, settings);
    if (!video->create(video_capture)) {
        return false;
    }

    //status_->setVideoStatus(SourceStatus::kSuccess);
    return true;
}

bool MediaStream::addAudio(const AudioSettings& settings) {
    if (audio || !outputs.empty() || !settings.device) {
        return false;
    }

    auto audio_capture = pipeline_->ensureAudioCapture(settings.device);
    if (!audio_capture) {
        return false;
    }

    audio = std::make_unique<AudioSource>(bin_, settings);
    if (!audio->create(audio_capture)) {
        return false;
    }

    return true;
}

bool MediaStream::addOutput(const std::string& id, const OutputSettings& settings) {
    auto output = std::make_unique<MediaOutput>(bin_, settings);
    if (!output->create()) {
        return false;
    }

    if (video) {
        if (!output->addVideo(video.get())) {
            return false;
        }
    }

    if (audio) {
        if (!output->addAudio(audio.get())) {
            return false;
        }
    }
    if (!output->syncState()) {
        return false;
    }

    outputs.emplace(id, std::move(output));
    return true;
}

bool MediaStream::removeVideo() {
    return false;
}

bool MediaStream::removeAudio() {
    return false;
}

bool MediaStream::syncOutputs(std::map<std::string, OutputSettings> settings) {
    if (!video && !audio) {
        return false;
    }

    auto outputs_it = outputs.begin();
    while (outputs_it != outputs.end()) {
        auto settings_it = settings.find(outputs_it->first);
        if (settings.end() == settings_it) {
            outputs_it = outputs.erase(outputs_it);
            continue;
        }

        if (!settings_it->second.enabled) {
            outputs_it = outputs.erase(outputs_it);
            continue;
        }

        settings.erase(settings_it);
        outputs_it++;
    }

    for (const auto& settings_it : settings) {
        if (!addOutput(settings_it.first, settings_it.second)) {
            return false;
        }
    }

    return true;
}

bool MediaStream::update(const StreamSettings& settings) {
    if (settings.video && settings.video->device) {
        if (!video) {
            // video settings added
            if (!addVideo(*settings.video)) {
                return false;
            }
        } else if (!video->update(*settings.video)) {
            // video settings changed
            if (!removeVideo()) {
                return false;
            }
        }

    } else if (video) {
        // video settings removed
        if (!removeVideo()) {
            return false;
        }
    }
if (settings.audio && settings.audio->device) {
    if (!audio) {
        // Audio settings added
        if (!addAudio(*settings.audio)) {
            return false;
        }
    } else if (!audio->update(*settings.audio)) {
        // Audio settings changed
        if (!removeAudio()) {
            return false;
        }
    }
} else if (audio) {
    // Audio settings removed
    if (!removeAudio()) {
        return false;
    }
}
    return syncOutputs(settings.outputs);
}

MediaStream::~MediaStream() {
    // Release outputs first so they can release requested tee pads and remove their branches
    outputs.clear();

    // Then release sources (video/audio) which may rely on outputs having already cleaned up
    video.reset();
    audio.reset();

    if (bin_) {
        gst_bin_remove(GST_BIN(pipeline_->getElement()), bin_);
        bin_ = nullptr;
    }
}

GstElement* MediaStream::createChildBin() {
    return ::createChildBin(bin_);
}

GstElement* MediaStream::getElement() {
    return bin_;
}

bool MediaStream::onError(GstElement* element) {
    auto it = outputs.begin();
    while (it != outputs.end()) {
        auto& output = it->second;
        if (gst_object_has_as_ancestor(GST_OBJECT(element), GST_OBJECT(output->getElement()))) {
            outputs.erase(it);
            return true;
        }
        ++it;
    }

    return false;
}