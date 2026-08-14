#include <media_stream.h>

#include <video_source.h>
#include <audio_source.h>
#include <media_output.h>

#include <config_manager.h>

#include <gst/gst.h>
#include <cstdio>
#include <deque>

#include <stream_state_store.h>

MediaStream::MediaStream(const std::string& id, StreamStateStore* stream_states) :
    id_(id), stream_states_(stream_states) {
}

bool MediaStream::create(const StreamSettings& settings) {
    pipeline = gst_pipeline_new(nullptr);
    if (!pipeline) {
        return false;
    }

    bus = gst_element_get_bus(GST_ELEMENT(pipeline));
    if (!bus) {
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

bool MediaStream::start() {
    auto status = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
    if (status == GST_STATE_CHANGE_FAILURE) {
        ProcessError();
        return false;
    }

    return true;
}

bool MediaStream::addVideo(const VideoSettings& settings) {
    if (video || !outputs.empty() || !settings.device) {
        return false;
    }

    video = std::make_unique<VideoSource>(pipeline, settings, stream_states_, id_);
    if (!video->create()) {
        return false;
    }

    return true;
}

bool MediaStream::addAudio(const AudioSettings& settings) {
    if (audio || !outputs.empty() || !settings.device) {
        return false;
    }

    audio = std::make_unique<AudioSource>(pipeline, settings);
    if (!audio->create()) {
        return false;
    }

    return true;
}

bool MediaStream::addOutput(const std::string& id, const OutputSettings& settings) {
    auto output = std::make_unique<MediaOutput>(pipeline, settings);
    if (!output->create()) {
        return false;
    }

    if (video) {
        if (!output->addVideo(video->video_tee)) {
            return false;
        }
    }

    if (audio) {
        if (!output->addAudio(audio->audio_tee)) {
            return false;
        }
    }

    outputs.emplace(id, std::move(output));
    return true;
}

bool MediaStream::IsOutputsEmpty() {
    return outputs.empty();
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
            // remove output
            return false;
        }

        if (!settings_it->second.enabled) {
            // remove output
            return false;
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

bool MediaStream::onError(GstElement* src) {
    if (video && video->video_bin == src) {
        printf("video source failed\n");
        video->status = SourceStatus::kFail;
        return false;
    }

    if (audio && audio->audio_bin == src) {
        printf("audio source failed\n");
        audio->status = SourceStatus::kFail;
        return false;
    }

    for (auto& it : outputs) {
        auto& output = it.second;
        if (output->output_bin == src) {
            printf("output:%s failed\n", it.first.c_str());
            output->status = OutputStatus::kFail;
            return false;
        }
    }

    return false;
}

bool MediaStream::ProcessError() {
    return ProcessMessage(GST_MESSAGE_ERROR);
}

bool MediaStream::ProcessMessage(uint64_t mask) {
    //GstMessage* msg = gst_bus_pop(bus);
    GstMessage* msg = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(mask));
    if (!msg) {
        return true;
    }

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
    {
        std::deque<GstElement*> stack;
        printf("!!!pipeline error!!!\n");

        GstObject* obj = GST_MESSAGE_SRC(msg);
        while (obj) {
            stack.push_front(GST_ELEMENT(obj));

            g_print("%s (%s)\n",
                GST_OBJECT_NAME(obj),
                G_OBJECT_TYPE_NAME(obj));

            obj = gst_object_get_parent(obj);
        }

        GError* err = NULL;
        gchar* debug = NULL;

        gst_message_parse_error(msg, &err, &debug);

        g_printerr("Error from %s: %s\n",
            GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)),
            err->message);

        g_error_free(err);
        g_free(debug);

        if (stack.size() < 2) {
            return false;
        }

        return onError(stack[1]);
    }

    case GST_MESSAGE_EOS:
        gst_message_unref(msg);
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
        g_print("%s (%s): %s -> %s (pending: %s)\n",
            GST_OBJECT_NAME(obj),
            G_OBJECT_TYPE_NAME(obj),
            gst_element_state_get_name(old_state),
            gst_element_state_get_name(new_state),
            gst_element_state_get_name(pending));

        if (obj == GST_OBJECT(pipeline) && GST_STATE_PLAYING == new_state) {
            printf("!!!PLAYING!!!\n");
            playing_ = true;
        }

        break;
    }

    case GST_MESSAGE_ELEMENT:
    {
        const GstStructure* structure = gst_message_get_structure(msg);
        if (!structure) {
            break;
        }

        if (!gst_structure_has_name(structure, "level")) {
            break;
        }

        const GValue* peak = gst_structure_get_value(structure, "peak");
        if (!peak) {
            break;
        }

        GValueArray* array = static_cast<GValueArray*>(g_value_get_boxed(peak));
        guint channels = array->n_values;
        if (channels <= 0) {
            break;
        }

        double sum = 0.0;
        for (guint i = 0; i < array->n_values; ++i) {
            const GValue* value = &array->values[i];
            if (G_VALUE_HOLDS_DOUBLE(value)) {
                sum += g_value_get_double(value);
            }
        }

        sum /= channels;

        stream_states_->setAudioLevel(id_, sum);
        break;
    }

    default:
        break;
    }

    gst_message_unref(msg);
    return true;
}

bool MediaStream::update(const StreamSettings& settings) {
    if (settings.video && settings.video->device) {
        if (!video) {
            // video settings added
            if (!addVideo(*settings.video)) {
                return false;
            }
        } else if (video->settings != *settings.video) {
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
            // audio settings added
            if (!addAudio(*settings.audio)) {
                return false;
            }
        } else if (audio->settings != *settings.audio) {
            // audio settings changed
            if (!removeAudio()) {
                return false;
            }
        }
    } else if (audio) {
        // audio settings removed
        if (!removeAudio()) {
            return false;
        }
    }

    return syncOutputs(settings.outputs);
}


StreamStatus MediaStream::getStatus() {
    StreamStatus status;

    if (video) {
        status.video_status = video->status;
    }

    if (audio) {
        status.audio_status = audio->status;
    }

    for (auto& it : outputs) {
        status.output_status[it.first] = it.second->status;
    }

    return status;
}

void MediaStream::startVideoPreview() {
    if (video && !video_preview_started_) {
        video->startVideoPreview();
        video_preview_started_ = true;
    }
}

void MediaStream::stopVideoPreview() {
    if (video && video_preview_started_) {
        video->stopVideoPreview();
        video_preview_started_ = false;
    }
}

MediaStream::~MediaStream() {
    video.reset();
    audio.reset();
    outputs.clear();

    if (bus) {
        gst_object_unref(bus);
    }

    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        gst_object_unref(pipeline);
    }
}