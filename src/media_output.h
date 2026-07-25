#pragma once

struct MediaOutput {
    GstElement* pipeline;
    OutputSettings settings;
    GstElement* output_bin{nullptr};
    GstElement* mux{nullptr};

    MediaOutput(GstElement* p, const OutputSettings& s) :
        pipeline(p), settings(s) {
    }

    bool create() {
        output_bin = gst_bin_new(NULL);
        if (!output_bin) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(pipeline), output_bin)) {
            return false;
        }

        if (settings.type == OutputSettings::Type::RTMP) {
            mux = gst_element_factory_make("flvmux", NULL);
            if (!mux) {
                return false;
            }

            g_object_set(mux,
                "streamable", TRUE,
                NULL);

            GstElement* sink = gst_element_factory_make("rtmpsink", NULL);
            if (!sink) {
                return false;
            }

            g_object_set(sink,
                "location", settings.url.c_str(),
                NULL);

            gst_bin_add_many(GST_BIN(output_bin), mux, sink, NULL);
            if (!gst_element_link(mux, sink)) {
                return false;
            }

            return true;
        }

        return false;
    }

    bool addVideo(GstElement* video_tee) {
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

        if (!gst_bin_add(GST_BIN(output_bin), vqueue)) {
            return false;
        }

        GstPad* vtee_src_pad = gst_element_request_pad_simple(video_tee, "src_%u");
        if (!vtee_src_pad) {
            return false;
        }

        GstPad* vqueue_sink_pad = gst_element_get_static_pad(vqueue, "sink");
        if (!vqueue_sink_pad) {
            return false;
        }

        GstPad* sink_ghost = gst_ghost_pad_new("vsink", vqueue_sink_pad);
        if (!sink_ghost) {
            return false;
        }

        if (!gst_element_add_pad(output_bin, sink_ghost)) {
            return false;
        }

        GstPadLinkReturn ret = gst_pad_link(vtee_src_pad, sink_ghost);
        if (ret != GST_PAD_LINK_OK) {
            printf("failed to connect video_tee to video_queue");
            return false;
        }

        GstPad* vqueue_src_pad = gst_element_get_static_pad(vqueue, "src");
        if (!vqueue_src_pad) {
            return false;
        }

        GstPad* mux_video_pad = gst_element_request_pad_simple(mux, "video");
        if (!mux_video_pad) {
            return false;
        }

        ret = gst_pad_link(vqueue_src_pad, mux_video_pad);
        if (ret != GST_PAD_LINK_OK) {
            printf("failed to connect output to video_queue to mux");
            return false;
        }

        return true;
    }


    bool addAudio(GstElement* audio_tee) {
        GstElement* aqueue = gst_element_factory_make("queue", NULL);
        if (!aqueue) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(output_bin), aqueue)) {
            return false;
        }

        GstPad* atee_src_pad = gst_element_request_pad_simple(audio_tee, "src_%u");
        if (!atee_src_pad) {
            return false;
        }

        GstPad* aqueue_sink_pad = gst_element_get_static_pad(aqueue, "sink");
        if (!aqueue_sink_pad) {
            return false;
        }

        GstPad* sink_ghost = gst_ghost_pad_new("asink", aqueue_sink_pad);
        if (!sink_ghost) {
            return false;
        }

        GstPadLinkReturn ret = gst_pad_link(atee_src_pad, sink_ghost);
        if (ret != GST_PAD_LINK_OK) {
            printf("failed to connect audio_tee to audio_queue");
        }

        GstPad* aqueue_src_pad = gst_element_get_static_pad(aqueue, "src");
        if (!aqueue_src_pad) {
            return false;
        }

        GstPad* mux_audio_pad = gst_element_request_pad_simple(mux, "audio");
        if (!mux_audio_pad) {
            return false;
        }

        ret = gst_pad_link(aqueue_src_pad, mux_audio_pad);
        if (ret != GST_PAD_LINK_OK) {
            printf("failed to connect output to audio_queue to mux");
            return false;
        }

        return true;
    }
};