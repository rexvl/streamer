#include "media_output.h"

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <cstdio>
#include <media_stream.h>
#include <video_source.h>
#include <audio_source.h>
#include <media_utils.h>

MediaOutput::MediaOutput(GstElement* stream_bin, const OutputSettings& s) :
    stream_bin_(stream_bin), settings_(s) {
    printf("MediaOutput::MediaOutput: id=%s\n", settings_.id.c_str());
}

MediaOutput::~MediaOutput() {
    printf("MediaOutput::~MediaOutput: id=%s\n", settings_.id.c_str());

    auto pad_to_probe = video_tee_pad_ ? video_tee_pad_ : audio_tee_pad_;
    if (pad_to_probe) {
        auto probe_id = gst_pad_add_probe(
            pad_to_probe,
            GST_PAD_PROBE_TYPE_IDLE,
                [](GstPad* pad, GstPadProbeInfo* info, gpointer user_data) -> GstPadProbeReturn {
                auto* self = static_cast<MediaOutput*>(user_data);
                self->unlink();
                return GST_PAD_PROBE_REMOVE;
            },
            this, NULL);

        if (probe_id) {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this]() {
                return !output_bin_;
            });
        }
    } else if (output_bin_) {
        unlink();
    }

    if (video_) {
        video_->removeSrcPad(video_tee_pad_);
        video_tee_pad_ = nullptr;
    }

    if (audio_) {
        audio_->removeSrcPad(audio_tee_pad_);
        audio_tee_pad_ = nullptr;
    }
}

void MediaOutput::unlink() {
    if (video_tee_pad_ && sink_ghost_) {
        gst_pad_unlink(video_tee_pad_, sink_ghost_);
    }

    if (audio_tee_pad_ && audio_sink_ghost_) {
        gst_pad_unlink(audio_tee_pad_, audio_sink_ghost_);
    }

    gst_element_set_state(output_bin_, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(stream_bin_), output_bin_);

    {
        std::scoped_lock<std::mutex> lk(mutex_);
        output_bin_ = nullptr;
    }

    cv_.notify_one();
}

bool MediaOutput::create() {
    output_bin_ = createChildBin(stream_bin_);
    if (!output_bin_) {
        return false;
    }

    std::string rtmp_prefix = "rtmp://";
    if (!settings_.url.compare(0, rtmp_prefix.size(), rtmp_prefix)) {
        mux_ = gst_element_factory_make("flvmux", NULL);
        if (!mux_) {
            return false;
        }

        g_object_set(mux_,
            "streamable", TRUE,
            NULL);

        auto sink = gst_element_factory_make("rtmpsink", NULL);
        if (!sink) {
            return false;
        }

        g_object_set(sink,
            "location", settings_.url.c_str(),
            "async", FALSE,  // Important: do not block pipeline state changes
            "sync", FALSE,   // Prevents the sink from depending on the old pipeline clock
            NULL);

        gst_bin_add_many(GST_BIN(output_bin_), mux_, sink, NULL);
        if (!gst_element_link(mux_, sink)) {
            return false;
        }

        return true;
    }

    return false;
}

bool MediaOutput::addVideo(VideoSource* video) {
    vqueue_ = gst_element_factory_make("queue", NULL);
    if (!vqueue_) {
        return false;
    }

    g_object_set(vqueue_,
        "leaky", 1, // drop old buffers
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 2 * GST_SECOND,
        NULL);

    if (!gst_bin_add(GST_BIN(output_bin_), vqueue_)) {
        return false;
    }

    if (!gst_element_link(vqueue_, mux_)) {
        return false;
    }

    if (!video_tee_pad_) {
        video_tee_pad_ = video->getNextSrcPad();
        if (!video_tee_pad_) {
            return false;
        }
    }

    sink_ghost_ = add_ghost_pad(output_bin_, vqueue_, "sink", "vsink");
    if (!sink_ghost_) {
        return false;
    }

    GstPadLinkReturn ret = gst_pad_link(video_tee_pad_, sink_ghost_);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect video_tee to video_queue");
        return false;
    }

    video_ = video;
    return true;
}

bool MediaOutput::addAudio(AudioSource* audio) {
    aqueue_ = gst_element_factory_make("queue", NULL);
    if (!aqueue_) {
        return false;
    }

    g_object_set(aqueue_,
        "leaky", 1, // drop old buffers
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 2 * GST_SECOND,
        NULL);

    if (!gst_bin_add(GST_BIN(output_bin_), aqueue_)) {
        return false;
    }

    auto mux_audio_pad = gst_element_request_pad_simple(mux_, "audio");
    if (!mux_audio_pad) {
        return false;
    }

    auto aqueue_src_pad = gst_element_get_static_pad(aqueue_, "src");
    GstPadLinkReturn ret = gst_pad_link(aqueue_src_pad, mux_audio_pad);
    gst_object_unref(aqueue_src_pad);
    gst_object_unref(mux_audio_pad);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect audio_queue to mux: %d\n", ret);
        return false;
    }

    if (!audio_tee_pad_) {
        audio_tee_pad_ = audio->getNextSrcPad();
        if (!audio_tee_pad_) {
            return false;
        }
    }

    audio_sink_ghost_ = add_ghost_pad(output_bin_, aqueue_, "sink", "asink");
    if (!audio_sink_ghost_) {
        return false;
    }

    ret = gst_pad_link(audio_tee_pad_, audio_sink_ghost_);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect audio_tee to audio_queue\n");
        return false;
    }

    audio_ = audio;
    return true;
}

bool MediaOutput::syncState() {
    return gst_element_sync_state_with_parent(output_bin_);
}

GstElement* MediaOutput::getElement() const {
    return output_bin_;
}