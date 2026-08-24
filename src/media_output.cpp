#include "media_output.h"

#include <gst/gst.h>
#include <cstdio>

MediaOutput::MediaOutput(GstElement* p, const OutputSettings& s) :
    pipeline_(p), settings_(s) {
}

MediaOutput::~MediaOutput() {
    // Ensure any asynchronous restart has finished before we start tearing down
    if (restart_future_.valid()) {
        restart_future_.wait();
    }

    // Remove any blocking probes we added on the source tee pads
    if (audio_tee_pad_ && audio_probe_id_ > 0) {
        gst_pad_remove_probe(audio_tee_pad_, audio_probe_id_);
        audio_probe_id_ = 0;
    }

    if (video_tee_pad_ && video_probe_id_ > 0) {
        gst_pad_remove_probe(video_tee_pad_, video_probe_id_);
        video_probe_id_ = 0;
    }

    // Release request pads back to their tee elements
    if (audio_tee_pad_) {
        // unlink and remove ghost pad from our output_bin_ first
        if (output_bin_) {
            GstPad* asink = gst_element_get_static_pad(output_bin_, "asink");
            if (asink) {
                gst_pad_unlink(audio_tee_pad_, asink);
                gst_element_remove_pad(output_bin_, asink);
                gst_object_unref(asink);
            }
        }

        GstElement* tee = GST_PAD_PARENT(audio_tee_pad_);
        if (GST_IS_ELEMENT(tee)) {
            gst_element_release_request_pad(tee, audio_tee_pad_);
        }
        gst_object_unref(audio_tee_pad_);
        audio_tee_pad_ = nullptr;
    }

    if (video_tee_pad_) {
        // unlink and remove ghost pad from our output_bin_ first
        if (output_bin_) {
            GstPad* vsink = gst_element_get_static_pad(output_bin_, "vsink");
            if (vsink) {
                gst_pad_unlink(video_tee_pad_, vsink);
                gst_element_remove_pad(output_bin_, vsink);
                gst_object_unref(vsink);
            }
        }

        GstElement* tee = GST_PAD_PARENT(video_tee_pad_);
        if (GST_IS_ELEMENT(tee)) {
            gst_element_release_request_pad(tee, video_tee_pad_);
        }
        gst_object_unref(video_tee_pad_);
        video_tee_pad_ = nullptr;
    }

    // Ensure branch is shut down. Do not remove/unref output_bin_ here: it is owned by the
    // pipeline (we added it with gst_bin_add) and pipeline teardown is responsible
    // for final unref/removal. We only need to remove internal probes and clear
    // our local references to pads.
    if (output_bin_) {
        if (sink_pad_ && sink_probe_id_ > 0) {
            gst_pad_remove_probe(sink_pad_, sink_probe_id_);
            sink_probe_id_ = 0;
        }
        if (sink_pad_) {
            gst_object_unref(sink_pad_);
            sink_pad_ = nullptr;
        }

        if (vqueue_src_pad_ && vqueue_probe_id_ > 0) {
            gst_pad_remove_probe(vqueue_src_pad_, vqueue_probe_id_);
            vqueue_probe_id_ = 0;
        }
        if (vqueue_src_pad_) {
            gst_object_unref(vqueue_src_pad_);
            vqueue_src_pad_ = nullptr;
        }

        if (aqueue_src_pad_ && aqueue_probe_id_ > 0) {
            gst_pad_remove_probe(aqueue_src_pad_, aqueue_probe_id_);
            aqueue_probe_id_ = 0;
        }
        if (aqueue_src_pad_) {
            gst_object_unref(aqueue_src_pad_);
            aqueue_src_pad_ = nullptr;
        }

        // Set bin to NULL so internal threads stop, but leave it added to the pipeline
        gst_element_set_state(output_bin_, GST_STATE_NULL);

        // Do not call gst_bin_remove or gst_object_unref on output_bin_ here;
        // the pipeline maintains ownership and will clean it up during pipeline teardown.
        output_bin_ = nullptr; // clear our local pointer
    }

    if (mux_) {
        gst_object_unref(mux_);
        mux_ = nullptr;
    }
}

GstPadProbeReturn MediaOutput::sink_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<MediaOutput*>(user_data);

    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        self->packet_count_++;
    }

    return GST_PAD_PROBE_OK;
}

bool MediaOutput::create() {
    output_bin_ = gst_bin_new(NULL);
    if (!output_bin_) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline_), output_bin_)) {
        return false;
    }

    if (settings_.type == OutputSettings::Type::RTMP) {
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

        sink_pad_ = gst_element_get_static_pad(sink, "sink");
        if (!sink_pad_) {
            return false;
        }

        sink_probe_id_ = gst_pad_add_probe(sink_pad_, GST_PAD_PROBE_TYPE_BUFFER, sink_probe, this, NULL);

        return true;
    }

    return false;
}

bool MediaOutput::addVideo(GstElement* video_tee) {
    GstElement* vqueue = gst_element_factory_make("queue", NULL);
    if (!vqueue) {
        return false;
    }

    g_object_set(vqueue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 2000 * GST_MSECOND,
        "leaky", 0,
        NULL);

    if (!gst_bin_add(GST_BIN(output_bin_), vqueue)) {
        return false;
    }

    if (!video_tee_pad_) {
        video_tee_pad_ = gst_element_request_pad_simple(video_tee, "src_%u");
        if (!video_tee_pad_) {
            return false;
        }
    }

    GstPad* vqueue_sink_pad = gst_element_get_static_pad(vqueue, "sink");
    if (!vqueue_sink_pad) {
        return false;
    }

    GstPad* sink_ghost = gst_ghost_pad_new("vsink", vqueue_sink_pad);
    if (!sink_ghost) {
        return false;
    }

    if (!gst_element_add_pad(output_bin_, sink_ghost)) {
        return false;
    }

    GstPadLinkReturn ret = gst_pad_link(video_tee_pad_, sink_ghost);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect video_tee to video_queue");
        return false;
    }

    vqueue_src_pad_ = gst_element_get_static_pad(vqueue, "src");
    if (!vqueue_src_pad_) {
        return false;
    }

    vqueue_probe_id_ = gst_pad_add_probe(
        vqueue_src_pad_,
        GST_PAD_PROBE_TYPE_BUFFER,
        drop_until_keyframe,
        this,
        nullptr
    );

    auto mux_video_pad = gst_element_request_pad_simple(mux_, "video");
    if (!mux_video_pad) {
        return false;
    }

    ret = gst_pad_link(vqueue_src_pad_, mux_video_pad);
    gst_object_unref(mux_video_pad);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect output to video_queue to mux");
        return false;
    }

    return true;
}

bool MediaOutput::addAudio(GstElement* audio_tee) {
    GstElement* aqueue = gst_element_factory_make("queue", NULL);
    if (!aqueue) {
        return false;
    }

    // Set stable, valid GstQueue properties
    g_object_set(
        aqueue,
        "leaky", 2, // GST_QUEUE_LEAK_DOWNSTREAM
        "max-size-buffers", 1,
        nullptr
    );

    // Queue is returned inside output_bin_ to ensure the entire branch resets cleanly together
    if (!gst_bin_add(GST_BIN(output_bin_), aqueue)) {
        return false;
    }

    if (!audio_tee_pad_) {
        audio_tee_pad_ = gst_element_request_pad_simple(audio_tee, "src_%u");
        if (!audio_tee_pad_) {
            return false;
        }
    }

    // Create a boundary ghost pad targeting the internal queue sink pad
    GstPad* aqueue_sink_pad = gst_element_get_static_pad(aqueue, "sink");
    GstPad* sink_ghost = gst_ghost_pad_new("asink", aqueue_sink_pad);
    gst_object_unref(aqueue_sink_pad);
    if (!sink_ghost) {
        return false;
    }

    if (!gst_element_add_pad(output_bin_, sink_ghost)) {
        return false;
    }

    // Link the stable source tee pad directly to the output_bin boundary ghost pad
    GstPadLinkReturn ret = gst_pad_link(audio_tee_pad_, sink_ghost);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect audio_tee to output_bin ghost pad\n");
        return false;
    }

    // Link internal queue src pad to internal flvmux pad
    GstPad* aqueue_src_pad = gst_element_get_static_pad(aqueue, "src");
    auto mux_audio_pad = gst_element_request_pad_simple(mux_, "audio");
    ret = gst_pad_link(aqueue_src_pad, mux_audio_pad);
    gst_object_unref(aqueue_src_pad);
    gst_object_unref(mux_audio_pad);
    if (ret != GST_PAD_LINK_OK) {
        printf("failed to connect internal queue to flvmux\n");
        return false;
    }

    return true;
}

GstPadProbeReturn MediaOutput::tee_pad_block(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<MediaOutput*>(user_data);

    if (pad == self->audio_tee_pad_) {
        self->audio_tee_blocked_ = true;
        printf("Audio source tee BLOCKED\n");
    } else if (pad == self->video_tee_pad_) {
        self->video_tee_blocked_ = true;
        printf("Video source tee BLOCKED\n");
    }

    if (!self->video_tee_blocked_ || !self->audio_tee_blocked_) {
        return GST_PAD_PROBE_OK;
    }

    printf("Launching std::async for safe restart...\n");

    // Run restart asynchronously to prevent locking the blocked audio capture thread
    self->restart_future_ = std::async(std::launch::async, [self]() {
        printf("[std::async] Safe context reached. Restarting entire bin...\n");
        self->restart();
    });

    return GST_PAD_PROBE_OK;
}

void MediaOutput::restart() {
    // 1. Safely drop the entire streaming bin into NULL state
    gst_element_set_state(output_bin_, GST_STATE_NULL);
    printf("Branch elements changed to NULL state\n");

    // 2. Bring the entire bin back to PLAYING state
    gst_element_set_state(output_bin_, GST_STATE_PLAYING);
    printf("Branch elements changed to PLAYING state\n");

    // 3. Remove the blocking probe from the source tee pad to wake up the audio stream
    if (audio_tee_pad_ && audio_probe_id_ > 0) {
        gst_pad_remove_probe(audio_tee_pad_, audio_probe_id_);
        audio_probe_id_ = 0;
        printf("Audio source tee UNBLOCKED, data streaming resumed\n");
    }

    if (video_tee_blocked_ && video_probe_id_ > 0) {
        gst_pad_remove_probe(video_tee_pad_, video_probe_id_);
        video_probe_id_ = 0;
        printf("Video source tee UNBLOCKED, data streaming resumed\n");
    }

    audio_tee_blocked_ = false;
    video_tee_blocked_ = false;
}

bool MediaOutput::blockTeePads() {
    video_tee_blocked_ = true;
    audio_tee_blocked_ = true;

    if (audio_tee_pad_) {
        // Enforce a hard downstream block right at the audio source tee pad exit
        audio_probe_id_ = gst_pad_add_probe(
            audio_tee_pad_,
            GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, // Blocks buffers before they touch output_bin_
            tee_pad_block,
            this,
            nullptr
        );

        printf("Probe added on audio source tee pad, id=%lu\n", audio_probe_id_);
        audio_tee_blocked_ = false;
    }

    if (video_tee_pad_) {
        video_probe_id_ = gst_pad_add_probe(
            video_tee_pad_,
            GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, // Blocks buffers before they touch output_bin_
            tee_pad_block,
            this,
            nullptr
        );

        printf("Probe added on audio source tee pad, id=%lu\n", audio_probe_id_);
        video_tee_blocked_ = false;
        waiting_for_keyframe_ = true;
    }
    return false;
}

GstPadProbeReturn MediaOutput::drop_until_keyframe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<MediaOutput*>(user_data);

    if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER))
        return GST_PAD_PROBE_OK;

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    if (!self->waiting_for_keyframe_) {
        return GST_PAD_PROBE_OK;
    }

    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        return GST_PAD_PROBE_DROP;
    }

    printf("VIDEO KEYFRAME FOUND, resume video\n");

    self->waiting_for_keyframe_ = false;

    return GST_PAD_PROBE_OK;
}