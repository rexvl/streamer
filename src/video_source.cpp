#include <iostream>
#include <vector>
#include <gst/gst.h>

#include <video_source.h>
#include <stream_state_store.h>

VideoSource::VideoSource(const GstElement* p, const VideoSettings& vs, StreamStateStore* stream_states, const std::string& stream_id) :
    pipeline(p),
    settings(vs),
    stream_states_(stream_states),
    status(SourceStatus::kSuccess),
    stream_id_(stream_id) {
}

bool link(GstElement* tee, std::vector<GstElement*> sinks) {
    for (auto& sink : sinks) {
        GstPad* tee_src = gst_element_request_pad_simple(tee, "src_%u");
        if (!tee_src) {
            return false;
        }

        GstPad* sink_pad = gst_element_get_static_pad(sink, "sink");
        if (!sink_pad) {
            return false;
        }

        if (gst_pad_link(tee_src, sink_pad) != GST_PAD_LINK_OK) {
            return false;
        }
    }

    return true;
}

GstPadProbeReturn VideoSource::process_jpeg(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        return GST_PAD_PROBE_OK;
    }

    auto self = static_cast<VideoSource*>(user_data);
    self->setVideoPreview(map.data, map.size);

    gst_buffer_unmap(buffer, &map);
    return GST_PAD_PROBE_OK;
}

void VideoSource::setVideoPreview(const uint8_t* buffer, const size_t buffer_size) {
    auto video_preview = std::make_shared<VideoPreview>(buffer, buffer_size, preview_index_++);
    stream_states_->setVideoPreview(stream_id_, video_preview);
}

GstElement* VideoSource::create_preview(GstElement* video_bin) {
    auto queue = gst_element_factory_make("queue", NULL);
    if (!queue) {
        return nullptr;
    }

    g_object_set(queue, 
                 "leaky", 2,              // GST_QUEUE_LEAK_DOWNSTREAM
                 "max-size-buffers", 1,
                 nullptr);
    
    auto videorate = gst_element_factory_make("videorate", NULL);
    if (!videorate) {
        return nullptr;
    }

    auto videoscale = gst_element_factory_make("videoscale", NULL);
    if (!videoscale) {
        return nullptr;
    }

    auto videoconvert = gst_element_factory_make("videoconvert", NULL);
    if (!videoconvert) {
        return nullptr;
    }

    auto capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!capsfilter) {
        return false;
    }

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240,
        "framerate", GST_TYPE_FRACTION, 20, 1,
        nullptr
    );

    if (!caps) {
        return nullptr;
    }

    g_object_set(capsfilter,
                 "caps", caps,
                 nullptr);

    auto jpegenc = gst_element_factory_make("jpegenc", NULL); 
    if (!jpegenc) {
        return nullptr;
    }


    auto sink = gst_element_factory_make("fakesink", NULL);
    if (!sink) {
        return nullptr;
    }

    g_object_set(sink,
                 "sync", FALSE,
                 "async", FALSE,
                 nullptr);

    gst_bin_add_many(GST_BIN(video_bin), queue, videorate, videoscale, capsfilter, jpegenc, sink, NULL);

    if (!gst_element_link_many(queue, videorate, videoscale, capsfilter, jpegenc, sink, NULL)) {
        return nullptr;
    }

    GstPad* pad = gst_element_get_static_pad(jpegenc, "src");
    if (!pad) {
        return nullptr;
    }

    if (!gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, process_jpeg, this, 0)) {
        return nullptr;
    }

    return queue;
}


bool VideoSource::create() {
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

    GstCaps* capture_caps = gst_device_get_caps(settings.device);
    if (!capture_caps) {
        return false;
    }

    GstElement* capture_tee = gst_element_factory_make("tee", NULL);
    if (!capture_tee) {
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

    gst_bin_add_many(GST_BIN(video_bin), capture, capture_tee, enc, parser, NULL);

    if (!gst_element_link_many(capture, capture_tee, NULL)) {
        return false;
    }

    auto preview = create_preview(video_bin);
    if (!preview) {
        return false;
    }

    if (!link(capture_tee, { enc, preview })) {
        return false;
    }

    if (!gst_element_link_many(enc, parser, NULL)) {
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

    video_tee = gst_element_factory_make("tee", NULL);
    if (!video_tee) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline), video_tee)) {
        return false;
    }

    return gst_element_link(video_bin, video_tee);
}

void VideoSource::startVideoPreview() {
    printf("VideoSource::startVideoPreview\n");
}

void VideoSource::stopVideoPreview() {
    printf("VideoSource::stopVideoPreview\n");
}