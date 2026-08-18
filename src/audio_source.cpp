#include "audio_source.h"

#include <gst/gst.h>
#include <gst/gstdeviceprovider.h>

#include <atomic>

AudioSource::AudioSource(GstElement* p, const AudioSettings& as) :
    pipeline_(p), settings_(as) {
}

GstPadProbeReturn AudioSource::buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<AudioSource*>(user_data);
    if (self) {
        self->frame_count_.fetch_add(1, std::memory_order_relaxed);
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

    gst_bin_add_many(GST_BIN(audio_bin_), capture, audioconvert, level, enc, parser, NULL);
    if (!gst_element_link_many(capture, audioconvert, level, enc, parser, NULL)) {
        return false;
    }

    GstPad* source_pad = gst_element_get_static_pad(parser, "src");
    if (!source_pad) {
        return false;
    }

    GstPad* source_ghost = gst_ghost_pad_new("src", source_pad);
    if (!source_ghost) {
        return false;
    }

    if (!gst_element_add_pad(audio_bin_, source_ghost)) {
        return false;
    }

    gst_object_unref(source_pad);

    gst_pad_add_probe(source_ghost,
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
    auto fakesink = gst_element_factory_make("fakesink", NULL);

    g_object_set(fakesink,
                 "sync", FALSE,
                 "async", FALSE,
                 nullptr);

    if (!gst_bin_add(GST_BIN(pipeline_), fakesink)) {
        return false;
    }

    return gst_element_link_many(audio_bin_, audio_tee_, fakesink, NULL);
}

bool AudioSource::update(const AudioSettings& settings) {
    return settings_ == settings;
}

uint32_t AudioSource::consumeFrameCount() {
    return frame_count_.exchange(0, std::memory_order_relaxed);
}

GstElement* AudioSource::get_tee() {
    return audio_tee_;
}

bool AudioSource::operator==(const GstElement* other) const {
    return audio_bin_ == other;
}