#include "audio_source.h"

#include <gst/gst.h>
#include <gst/gstdeviceprovider.h>

#include <atomic>

AudioSource::AudioSource(GstElement* p, const AudioSettings& as) :
    pipeline(p), settings(as), status(SourceStatus::kSuccess) {
}

GstPadProbeReturn AudioSource::buffer_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<AudioSource*>(user_data);
    if (self) {
        self->frame_count.fetch_add(1, std::memory_order_relaxed);
    }
    return GST_PAD_PROBE_OK;
}

bool AudioSource::create() {
    if (!settings.device) {
        return false;
    }

    audio_bin = gst_bin_new(NULL);
    if (!audio_bin) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline), audio_bin)) {
        return false;
    }

    GstElement* capture = gst_device_create_element(settings.device, NULL);
    if (!capture) {
        return false;
    }

    GstElement* audioconvert = gst_element_factory_make("audioconvert", NULL);
    if (!audioconvert) {
        return false;
    }

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

    gst_bin_add_many(GST_BIN(audio_bin), capture, audioconvert, enc, parser, NULL);
    if (!gst_element_link_many(capture, audioconvert, enc, parser, NULL)) {
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

    if (!gst_element_add_pad(audio_bin, source_ghost)) {
        return false;
    }

    gst_object_unref(source_pad);

    gst_pad_add_probe(source_ghost,
        GST_PAD_PROBE_TYPE_BUFFER,
        buffer_probe,
        this,
        NULL);

    audio_tee = gst_element_factory_make("tee", NULL);
    if (!audio_tee) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline), audio_tee)) {
        return false;
    }

    return gst_element_link(audio_bin, audio_tee);
}
