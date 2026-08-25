#include "audio_source.h"

#include <gst/gst.h>
#include <gst/gstdeviceprovider.h>

#include <atomic>

AudioSource::AudioSource(GstElement* p, const AudioSettings& as) :
    pipeline_(p), settings_(as) {
}

AudioSource::~AudioSource() {
    // Remove pad probe on our source ghost pad so callbacks won't reference this
    if (source_ghost_pad_ && source_probe_id_ > 0) {
        gst_pad_remove_probe(source_ghost_pad_, source_probe_id_);
        source_probe_id_ = 0;
    }

    // Do not remove/unref elements that were added to the global pipeline here.
    // Just set their state to NULL and clear local pointers so pipeline owns final cleanup.
    if (audio_bin_) {
        gst_element_set_state(audio_bin_, GST_STATE_NULL);
        audio_bin_ = nullptr;
    }

    if (audio_tee_) {
        gst_element_set_state(audio_tee_, GST_STATE_NULL);
        audio_tee_ = nullptr;
    }

    if (fakesink_) {
        gst_element_set_state(fakesink_, GST_STATE_NULL);
        fakesink_ = nullptr;
    }
}

GstPadProbeReturn AudioSource::buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<AudioSource*>(user_data);
    if (self) {
        self->frame_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn AudioSource::capture_pad_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    //auto self = static_cast<VideoSource*>(user_data);

    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (!event) {
        return GST_PAD_PROBE_OK;
    }

    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
        GstCaps* caps = NULL;

        gst_event_parse_caps(event, &caps);

        if (caps) {
            gchar* caps_str = gst_caps_to_string(caps);

            g_print("[%s] CAPS: %s\n",
                GST_PAD_NAME(pad),
                caps_str);

            g_free(caps_str);
        }
    }

    return GST_PAD_PROBE_OK;
}

bool AudioSource::create() {
    if (!settings_.device) {
        return false;
    }

    audio_bin_ = gst_bin_new(NULL);
    if (!audio_bin_) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline_), audio_bin_)) {
        gst_object_unref(audio_bin_);
        audio_bin_ = nullptr;
        return false;
    }

    GstElement* capture = gst_device_create_element(settings_.device, NULL);
    if (!capture) {
        return false;
    }

    GstElement* audioconvert = gst_element_factory_make("audioconvert", NULL);
    if (!audioconvert) {
        return false;
    }

    GstElement* audioresample = gst_element_factory_make("audioresample", NULL);
    if (!audioresample) {
        return false;
    }

    GstElement* capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!capsfilter) {
        return false;
    }

    GstCaps* caps = gst_caps_new_simple(
        "audio/x-raw",
        "rate", G_TYPE_INT, settings_.sampleRate,
        "channels", G_TYPE_INT, settings_.channel_count,
        nullptr
    );

    if (!caps) {
        return false;
    }

    g_object_set(capsfilter, "caps", caps, nullptr);

    GstElement* level = gst_element_factory_make("level", NULL);
    if (!level) {
        return false;
    }

    g_object_set(level,
        "interval", (gint64)100 * GST_MSECOND,
        "post-messages", TRUE,
        NULL);

    GstElement* enc = gst_element_factory_make("voaacenc", NULL);
    if (!enc) {
        return false;
    }

    g_object_set(enc,
        "bitrate", 128000,
        NULL);

    GstElement* parser = gst_element_factory_make("aacparse", NULL);
    if (!parser) {
        return false;
    }

    gst_bin_add_many(GST_BIN(audio_bin_), capture, audioresample, audioconvert, capsfilter, level, enc, parser, NULL);
    if (!gst_element_link_many(capture, audioresample, audioconvert, capsfilter, level, enc, parser, NULL)) {
        return false;
    }

    GstPad* capture_pad = gst_element_get_static_pad(capture, "src");
    if (!capture_pad) {
        return false;
    }

    gst_pad_add_probe(capture_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, capture_pad_probe, this, NULL);

    GstPad* source_pad = gst_element_get_static_pad(parser, "src");
    if (!source_pad) {
        return false;
    }

    source_ghost_pad_ = gst_ghost_pad_new("src", source_pad);
    gst_object_unref(source_pad);
    if (!source_ghost_pad_) {
        return false;
    }

    if (!gst_element_add_pad(audio_bin_, source_ghost_pad_)) {
        gst_object_unref(source_ghost_pad_);
        source_ghost_pad_ = nullptr;
        return false;
    }

    source_probe_id_ = gst_pad_add_probe(source_ghost_pad_,
        GST_PAD_PROBE_TYPE_BUFFER,
        buffer_probe,
        this,
        NULL);

    audio_tee_ = gst_element_factory_make("tee", NULL);
    if (!audio_tee_) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline_), audio_tee_)) {
        return false;
    }

    // add dummy sink to avoid getting EOS on output issue
    fakesink_ = gst_element_factory_make("fakesink", NULL);

    g_object_set(fakesink_,
                 "sync", FALSE,
                 "async", FALSE,
                 nullptr);

    if (!gst_bin_add(GST_BIN(pipeline_), fakesink_)) {
        return false;
    }

    return gst_element_link_many(audio_bin_, audio_tee_, fakesink_, NULL);
}

bool AudioSource::update(const AudioSettings& settings) {
    return settings_ == settings;
}

void AudioSource::updateStats(std::shared_ptr<StreamStatus>& stats) {
    uint32_t frame_count = frame_count_.exchange(0, std::memory_order_relaxed);
    if (frame_count > 0) {
        stats->setAudioStatus(SourceStatus::kSuccess);
    }
}

GstElement* AudioSource::get_tee() {
    return audio_tee_;
}

bool AudioSource::operator==(const GstElement* other) const {
    return audio_bin_ == other;
}