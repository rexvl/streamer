#pragma once

struct AudioSource {
    GstElement* pipeline;
    AudioSettings settings;
    GstElement* audio_bin{ nullptr };
    GstElement* audio_tee{ nullptr };

    AudioSource(GstElement* p, const AudioSettings& as) :
        pipeline(p), settings(as) {
    }

    bool create(GstDevice* device) {
        audio_bin = gst_bin_new(NULL);
        if (!audio_bin) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(pipeline), audio_bin)) {
            return false;
        }

        GstElement* capture = gst_device_create_element(device, NULL);
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

        audio_tee = gst_element_factory_make("tee", NULL);
        if (!audio_tee) {
            return false;
        }

        if (!gst_bin_add(GST_BIN(pipeline), audio_tee)) {
            return false;
        }

        return gst_element_link(audio_bin, audio_tee);
    }
};