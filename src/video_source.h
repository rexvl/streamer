#pragma once

struct VideoSource {
    GstElement* pipeline;
    VideoSettings settings;
    GstElement* video_bin{ nullptr };
    GstElement* video_tee{ nullptr };
    std::atomic<uint32_t> frame_count{ 0 };
    SourceStatus status { SourceStatus::kSuccess };

    VideoSource(GstElement* p, const VideoSettings& vs) :
        pipeline(p), settings(vs) {
    }

    bool create() {
        if (!settings.device) {
            return false;
        }

        video_bin = gst_bin_new(NULL);
        if (!video_bin) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(pipeline), video_bin)) {
            return false;
        }

        GstElement* capture = gst_device_create_element(settings.device, NULL);
        if (!capture) {
            return false;
        }

        GstElement* enc = gst_element_factory_make("x264enc", NULL);
        if (!enc) {
            return false;
        }

        g_object_set(enc,
            "bitrate", 1000,
            NULL); 

        gst_util_set_object_arg(
            G_OBJECT(enc),
            "speed-preset",
            "ultrafast"
        );

        gst_util_set_object_arg(
            G_OBJECT(enc),
            "tune",
            "zerolatency"
        );

        g_object_set(enc,
            "key-int-max", 60,
            NULL);

        GstElement* parser = gst_element_factory_make("h264parse", NULL);
        if (!parser) {
            return false;
        }

        gst_bin_add_many(GST_BIN(video_bin), capture, enc, parser, NULL);

        if (!gst_element_link_many(capture, enc, parser, NULL)) {
            return false;
        }

        GstPad* source_pad = gst_element_get_static_pad(parser, "src");
        if (!source_pad) {
            return false;
        }

        GstPad* source_ghost = gst_ghost_pad_new("src", source_pad);
        if (!source_ghost) {
            gst_object_unref(source_pad);
            return false;
        }

        if (!gst_element_add_pad(video_bin, source_ghost)) {
            return false;
        }

        gst_object_unref(source_pad);
/*
        gst_pad_add_probe(source_ghost,
            GST_PAD_PROBE_TYPE_BUFFER,
            buffer_probe,
            this,
            NULL);
*/
        video_tee = gst_element_factory_make("tee", NULL);
        if (!video_tee) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(pipeline), video_tee)) {
            return false;
        }

        return gst_element_link(video_bin, video_tee);
    }
};
