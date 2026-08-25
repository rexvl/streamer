#include <iostream>
#include <vector>
#include <gst/gst.h>
#include <video_source.h>

VideoSource::VideoSource(const GstElement* p, const VideoSettings& settings, const std::shared_ptr<PreviewState>& preview) :
    pipeline_(p),
    settings_(settings),
    preview_(preview) {
    printf("VideoSource::VideoSource\n");
}

VideoSource::~VideoSource() {
    // Stop and remove preview branch if present
    remove_preview_branch();

    // Tear down video bin
    if (video_bin_) {
        // Stop internal threads for elements inside this bin. Do NOT remove/unref
        // the bin here because ownership is transferred to the pipeline.
        gst_element_set_state(video_bin_, GST_STATE_NULL);
        video_bin_ = nullptr; // clear our local pointer; pipeline owns the bin
    }

    // Tear down tee and dummy sink if present
    if (video_tee_) {
        // The tee is shared with other branches (outputs). Do not remove/unref it here.
        gst_element_set_state(video_tee_, GST_STATE_NULL);
        video_tee_ = nullptr;
    }
    if (fakesink_) {
        // Dummy sink added to pipeline; do not remove/unref here.
        gst_element_set_state(fakesink_, GST_STATE_NULL);
        fakesink_ = nullptr;
    }
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
    preview_->setPreview(video_preview);
}

GstElement* VideoSource::create_preview() {
    auto preview_bin = gst_bin_new("preview_bin");
    if (!preview_bin) {
        return nullptr;
    }

    auto queue = gst_element_factory_make("queue", nullptr);
    auto videorate = gst_element_factory_make("videorate", nullptr);
    auto videoscale = gst_element_factory_make("videoscale", nullptr);
    auto videoconvert = gst_element_factory_make("videoconvert", nullptr);
    auto capsfilter = gst_element_factory_make("capsfilter", nullptr);
    auto jpegenc = gst_element_factory_make("jpegenc", nullptr);
    auto sink = gst_element_factory_make("fakesink", nullptr);

    if (!queue ||
        !videorate ||
        !videoscale ||
        !videoconvert ||
        !capsfilter ||
        !jpegenc ||
        !sink) {
        if (queue) gst_object_unref(queue);
        if (videorate) gst_object_unref(videorate);
        if (videoscale) gst_object_unref(videoscale);
        if (videoconvert) gst_object_unref(videoconvert);
        if (capsfilter) gst_object_unref(capsfilter);
        if (jpegenc) gst_object_unref(jpegenc);
        if (sink) gst_object_unref(sink);

        gst_object_unref(preview_bin);
        return nullptr;
    }

    g_object_set(
        queue,
        "leaky", 2, // GST_QUEUE_LEAK_DOWNSTREAM
        "max-size-buffers", 1,
        nullptr
    );

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240,
        "framerate", GST_TYPE_FRACTION, 20, 1,
        nullptr
    );

    if (!caps) {
        gst_object_unref(preview_bin);
        return nullptr;
    }

    g_object_set(
        capsfilter,
        "caps", caps,
        nullptr
    );

    gst_caps_unref(caps);

    g_object_set(
        sink,
        "sync", FALSE,
        "async", FALSE,
        nullptr
    );

    gst_bin_add_many(
        GST_BIN(preview_bin),
        queue,
        videorate,
        videoscale,
        videoconvert,
        capsfilter,
        jpegenc,
        sink,
        nullptr
    );

    if (!gst_element_link_many(
        queue,
        videorate,
        videoscale,
        videoconvert,
        capsfilter,
        jpegenc,
        sink,
        nullptr)) {
        gst_object_unref(preview_bin);
        return nullptr;
    }

    GstPad* jpeg_src = gst_element_get_static_pad(
        jpegenc,
        "src"
    );

    if (!jpeg_src) {
        gst_object_unref(preview_bin);
        return nullptr;
    }


    printf("add process_jpeg\n");

    gst_pad_add_probe(
        jpeg_src,
        GST_PAD_PROBE_TYPE_BUFFER,
        process_jpeg,
        this,
        nullptr
    );

    gst_object_unref(jpeg_src);

    GstPad* queue_sink = gst_element_get_static_pad(
        queue,
        "sink"
    );

    if (!queue_sink) {
        gst_object_unref(preview_bin);
        return nullptr;
    }

    GstPad* ghost_sink = gst_ghost_pad_new(
        "sink",
        queue_sink
    );

    gst_object_unref(queue_sink);

    if (!ghost_sink) {
        gst_object_unref(preview_bin);
        return nullptr;
    }

    if (!gst_element_add_pad(
        preview_bin,
        ghost_sink)) {
        gst_object_unref(ghost_sink);
        gst_object_unref(preview_bin);
        return nullptr;
    }

    return preview_bin;
}

GstElement* VideoSource::createEncoder(const VideoSettings& settings) {
    GstElement* enc = nullptr;

    const guint fps = (settings.fps_n > 0 && settings.fps_d > 0) ? (settings.fps_n / settings.fps_d) : 30;
    const guint keyint_frames = fps * 2;

    switch (settings.codec) { 
        case VideoSettings::Codec::nvautogpuh264enc:
        {
            enc = gst_element_factory_make("nvautogpuh264enc", NULL);
            if (enc) {
                g_object_set(G_OBJECT(enc),
                    "bitrate", settings.bitrate * 1000,      // NVENC requires bps
                    "rc-mode", 1,                            // 1 = CBR (Constant Bitrate)
                    "gop-size", keyint_frames,               // or "idr-interval"
                    "iframeinterval", keyint_frames,         // I-frame interval
                    NULL);
            }
            break;
        }

        case VideoSettings::Codec::qsvh264enc:
        {
            enc = gst_element_factory_make("qsvh264enc", NULL);
            if (enc) {
                g_object_set(G_OBJECT(enc),
                    "bitrate", settings.bitrate,       // QSV requires Kbps
                    "rate-control", 1,                 // 1 = CBR (или MFX_RATECONTROL_CBR)
                    "gop-size", keyint_frames,         // GOP in frames
                    "gop-ref-dist", 1,                 // disable B-frames for strict CBR with no delays
                    NULL);
            }
            break;
        }

        case VideoSettings::Codec::x264enc:
        default:
        {
            enc = gst_element_factory_make("x264enc", NULL);
            if (enc) {
                g_object_set(enc,
                    "bitrate", settings.bitrate,
                    "key-int-max", keyint_frames,
                    "pass", 0,
                    "speed-preset", "ultrafast",
                    "tune", "zerolatency",
                    NULL);
            }
            break;
        }
    }

    return enc;
}

GstPadProbeReturn VideoSource::capture_src_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
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

GstPadProbeReturn VideoSource::encoder_sink_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
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

bool VideoSource::create() {
    if (!settings_.device) {
        return false;
    }

    video_bin_ = gst_bin_new(NULL);
    if (!video_bin_) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline_), video_bin_)) {
        return false;
    }

    GstElement* capture = gst_device_create_element(settings_.device, NULL);
    if (!capture) {
        return false;
    }

    GstCaps* capture_caps = gst_device_get_caps(settings_.device);
    if (!capture_caps) {
        return false;
    }

    capture_tee_ = gst_element_factory_make("tee", NULL);
    if (!capture_tee_) {
        return false;
    }

    GstElement* videorate = gst_element_factory_make("videorate", NULL);
    if (!videorate) {
        return false;
    }

    GstElement* videoscale = gst_element_factory_make("videoscale", NULL);
    if (!videoscale) {
        return false;
    }

    g_object_set(
        videoscale,
        "add-borders", TRUE,
        nullptr
    );

    GstElement* videoconvert = gst_element_factory_make("videoconvert", NULL);
    if (!videoconvert) {
        return false;
    }

    GstElement* capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!capsfilter) {
        return false;
    }

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "width", G_TYPE_INT, settings_.width,
        "height", G_TYPE_INT, settings_.height,
        "framerate", GST_TYPE_FRACTION, settings_.fps_n, settings_.fps_d,
        nullptr
    );

    if (!caps) {
        return false;
    }

    g_object_set(capsfilter, "caps", caps, nullptr);

    GstElement* enc = createEncoder(settings_);
    if (!enc) {
        return false;
    }

    GstElement* parser = gst_element_factory_make("h264parse", NULL);
    if (!parser) {
        return false;
    }

    gst_bin_add_many(GST_BIN(video_bin_), capture, capture_tee_, videorate, videoscale, videoconvert, capsfilter, enc, parser, NULL);

    if (!gst_element_link_many(capture, capture_tee_, videorate, videoscale, videoconvert, capsfilter, enc, parser, NULL)) {
        return false;
    }

    GstPad* capture_src_pad = gst_element_get_static_pad(capture, "src");
    if (capture_src_pad) {
        gst_pad_add_probe(capture_src_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, capture_src_probe, this, NULL);
        gst_object_unref(capture_src_pad);
    }

    GstPad* encoder_sink_pad = gst_element_get_static_pad(enc, "sink");
    if (encoder_sink_pad) {
        gst_pad_add_probe(encoder_sink_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, encoder_sink_probe, this, NULL);
        gst_object_unref(encoder_sink_pad);
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

    if (!gst_element_add_pad(video_bin_, source_ghost)) {
        return false;
    }

    gst_object_unref(source_pad);

    video_tee_ = gst_element_factory_make("tee", NULL);
    if (!video_tee_) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(pipeline_), video_tee_)) {
        return false;
    }

    // add dummy sink to avoid getting EOS on output issue
    auto fakesink = gst_element_factory_make("fakesink", NULL);
    fakesink_ = fakesink;

    g_object_set(fakesink,
        "sync", FALSE,
        "async", FALSE,
        nullptr);

    if (!gst_bin_add(GST_BIN(pipeline_), fakesink)) {
        return false;
    }

    return gst_element_link_many(video_bin_, video_tee_, fakesink, NULL);
}

bool VideoSource::update(const VideoSettings& settings) {
    bool ret = (settings_ == settings);

    if (!ret) {
        printf("!!!video settings changed!!!\n");
    }

    return ret;
}

void VideoSource::syncPreview() {
    const bool preview_enabled = preview_->isVideoPreviewEnabled();
    if (preview_enabled_ == preview_enabled) {
        return;
    }

    if (preview_enabled) {
        startVideoPreview();
    } else {
        stopVideoPreview();
    }

    preview_enabled_ = preview_enabled;
}

void VideoSource::startVideoPreview() {
    printf("VideoSource::startVideoPreview\n");

    auto pad = gst_element_get_static_pad(capture_tee_, "sink");
    if (!pad) {
        return;
    }

    g_print("[START] before add_probe this=%p\n", this);
    gulong probe_id = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_IDLE, start_preview_idle, this, nullptr);
    g_print("[START] after add_probe id=%lu this=%p\n", probe_id, this);

    gst_object_unref(pad);
}

GstPadProbeReturn VideoSource::start_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<VideoSource*>(user_data);
    g_print("[IDLE] ENTER this=%p pad=%p\n", self, pad);

    if (self->preview_starting_.exchange(true)) {
        return GST_PAD_PROBE_REMOVE;
    }

    self->add_preview_branch();
    self->preview_starting_ = false;

    return GST_PAD_PROBE_REMOVE;
}

bool VideoSource::add_preview_branch() {
    if (preview_bin_) {
        return true;
    }

    GstElement* preview_bin = create_preview();
    if (!preview_bin) {
        return false;
    }

    if (!gst_bin_add(GST_BIN(video_bin_), preview_bin)) {
        gst_object_unref(preview_bin);
        return false;
    }

    GstPad* tee_pad = gst_element_request_pad_simple(capture_tee_, "src_%u");
    if (!tee_pad) {
        gst_bin_remove(GST_BIN(video_bin_), preview_bin);
        return false;
    }

    GstPad* preview_sink = gst_element_get_static_pad(preview_bin, "sink");
    if (!preview_sink) {
        gst_element_release_request_pad(capture_tee_, tee_pad);
        gst_object_unref(tee_pad);
        gst_bin_remove(GST_BIN(video_bin_), preview_bin);
        return false;
    }

    GstPadLinkReturn ret = gst_pad_link(tee_pad, preview_sink);
    gst_object_unref(preview_sink);

    if (ret != GST_PAD_LINK_OK) {
        gst_element_release_request_pad(capture_tee_, tee_pad);
        gst_object_unref(tee_pad);
        gst_bin_remove(GST_BIN(video_bin_), preview_bin);
        return false;
    }

    preview_bin_ = preview_bin;
    preview_tee_pad_ = tee_pad;

    if (!gst_element_sync_state_with_parent(preview_bin_)) {
        remove_preview_branch();
        return false;
    }

    return true;
}

void VideoSource::stopVideoPreview() {
    printf("VideoSource::stopVideoPreview\n");
    if (!preview_bin_) {
        return;
    }

    auto capture_tee_sink_pad = gst_element_get_static_pad(capture_tee_, "sink");
    if (!capture_tee_sink_pad) {
        return;
    }

    gst_pad_add_probe(capture_tee_sink_pad, GST_PAD_PROBE_TYPE_IDLE, stop_preview_idle, this, nullptr);

    gst_object_unref(capture_tee_sink_pad);
    capture_tee_sink_pad = nullptr;

}

GstPadProbeReturn VideoSource::stop_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    auto self = static_cast<VideoSource*>(user_data);
    g_print("[IDLE] ENTER this=%p pad=%p\n", self, pad);

    if (self->preview_stopping_.exchange(true)) {
        return GST_PAD_PROBE_REMOVE;
    }

    self->remove_preview_branch();
    self->preview_stopping_ = false;

    return GST_PAD_PROBE_REMOVE;
}

void VideoSource::remove_preview_branch() {
    if (!preview_bin_) {
        return;
    }

    GstElement* preview_bin = preview_bin_;
    GstPad* tee_pad = preview_tee_pad_;

    preview_bin_ = nullptr;
    preview_tee_pad_ = nullptr;

    if (tee_pad) {
        GstPad* preview_sink = gst_element_get_static_pad(preview_bin, "sink");

        if (preview_sink) {
            gst_pad_unlink(tee_pad, preview_sink);
            gst_object_unref(preview_sink);
        }

        gst_element_release_request_pad(capture_tee_, tee_pad);

        gst_object_unref(tee_pad);
    }

    gst_element_set_state(preview_bin, GST_STATE_NULL);

    gst_bin_remove(GST_BIN(video_bin_), preview_bin);
}

bool VideoSource::operator==(GstElement* other) const {
    return video_bin_ == other;
}

void VideoSource::updateStats(std::shared_ptr<StreamStatus>& stats) {
    auto frame_count = frame_count_.exchange(0, std::memory_order_relaxed);
    if (frame_count > 10) {
        stats->setVideoStatus(SourceStatus::kSuccess);
    }
}

GstElement* VideoSource::get_tee() {
    return video_tee_;
}