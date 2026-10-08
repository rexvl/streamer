#include "audio_source.h"

#include <iostream>
#include <gst/gst.h>
#include <media_utils.h>

AudioSource::AudioSource(GstElement* stream_bin, const AudioSettings& settings) :
    stream_bin_(stream_bin),
    settings_(settings) {
    printf("AudioSource::AudioSource\n");
}

AudioSource::~AudioSource() {
    destroy();
}

GstElement* AudioSource::createEncoder(const AudioSettings& settings) {
    GstElement* enc = nullptr;

    switch (settings.codec) {
        case AudioSettings::Codec::AAC:
        default:
        {
            enc = gst_element_factory_make("voaacenc", NULL);
            if (enc) {
                g_object_set(enc,
                    "bitrate", settings.bitrate * 1000,
                    NULL);
            }
            break;
        }
    }

    return enc;
}

bool AudioSource::create(std::shared_ptr<MediaCapture>& capture) {
    audio_bin_ = createChildBin(stream_bin_);
    if (!audio_bin_) {
        return false;
    }

    auto queue = gst_element_factory_make("queue", NULL);
    if (!queue) {
        return false;
    }

    g_object_set(queue,
        "leaky", 2,
        "max-size-buffers", 20,
        "max-size-time", (guint64)0,
        "max-size-bytes", (guint)0,
        nullptr);

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
        "format", G_TYPE_STRING, "S16LE",
        "layout", G_TYPE_STRING, "interleaved",
        "rate", G_TYPE_INT, settings_.sampleRate,
        "channels", G_TYPE_INT, settings_.channel_count,
        nullptr
    );

    if (!caps) {
        return false;
    }

    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    GstElement* enc = createEncoder(settings_);
    if (!enc) {
        return false;
    }

    GstElement* parser = gst_element_factory_make("aacparse", NULL);
    if (!parser) {
        return false;
    }

    tee_ = gst_element_factory_make("tee", NULL);
    if (!tee_) {
        return false;
    }

    // Add dummy sink to avoid getting EOS on output disconnect
    auto fakesink = gst_element_factory_make("fakesink", NULL);
    if (!fakesink) {
        return false;
    }

    g_object_set(fakesink,
        "sync", FALSE,
        "async", FALSE,
        nullptr);

    gst_bin_add_many(GST_BIN(audio_bin_), queue, audioconvert, audioresample, capsfilter, enc, parser, tee_, fakesink, NULL);

    if (!gst_element_link_many(queue, audioconvert, audioresample, capsfilter, enc, parser, tee_, fakesink, NULL)) {
        return false;
    }

    auto sink_pad = add_ghost_pad(audio_bin_, queue, "sink", NULL);
    if (!sink_pad) {
        return false;
    }

    // Link encoder's sink to stream's sink
    ghost_pad_ = gst_ghost_pad_new(NULL, sink_pad);
    if (!ghost_pad_) {
        return false;
    }

    if (!gst_element_add_pad(stream_bin_, ghost_pad_)) {
        return false;
    }

    if (!gst_element_sync_state_with_parent(audio_bin_)) {
        printf("AudioSource::create: failed to sync video_bin_\n");
        return false;
    }

    auto src_pad = capture->linkNextSrcPad(ghost_pad_);
    if (!src_pad) {
        return false;
    }

    capture_ = capture;
    src_pad_ = src_pad;
    return true;
}

GstPad* AudioSource::getNextSrcPad() {
    auto src_pad = gst_element_request_pad_simple(tee_, "src_%u");
    if (!src_pad) {
        return nullptr;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(NULL, src_pad);
    if (!ghost_pad) {
        gst_element_release_request_pad(tee_, src_pad);
        gst_object_unref(src_pad);
        return nullptr;
    }

    if (!gst_element_add_pad(audio_bin_, ghost_pad)) {
        gst_object_unref(ghost_pad);
        gst_element_release_request_pad(tee_, src_pad);
        gst_object_unref(src_pad);
        return nullptr;
    }

    return ghost_pad;
}

void AudioSource::removeSrcPad(GstPad* ghost_pad) {
    auto src_pad = gst_ghost_pad_get_target(GST_GHOST_PAD(ghost_pad));
    gst_element_remove_pad(audio_bin_, ghost_pad);
    gst_element_release_request_pad(tee_, src_pad);
    gst_object_unref(src_pad);
}

bool AudioSource::update(const AudioSettings& settings) {
    bool ret = (settings_ == settings);

    if (!ret) {
        printf("!!!audio settings changed!!!\n");
    }

    return ret;
}

void AudioSource::destroy() {
    if (!audio_bin_) {
        return;
    }

    // 1. Unlink incoming pad from capture stream
    if (src_pad_ && ghost_pad_) {
        gst_pad_unlink(src_pad_, ghost_pad_);
    }

    // 2. Return request pad back to MediaCapture tee
    if (capture_ && src_pad_) {
        capture_->removeSrcPad(src_pad_);
        src_pad_ = nullptr;
        capture_.reset();
    }

    // 3. Remove ghost pad from stream_bin_
    if (ghost_pad_) {
        gst_element_remove_pad(stream_bin_, ghost_pad_);
        ghost_pad_ = nullptr;
    }

    // 4. Stop and remove encoder bin from stream_bin_
    gst_element_set_state(audio_bin_, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(stream_bin_), audio_bin_);
    audio_bin_ = nullptr;
}