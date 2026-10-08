#include <video_preview.h>
#include <media_utils.h>
#include <media_capture.h>
#include <gst/app/gstappsink.h>

VideoPreview::VideoPreview(MediaPipeline* pipeline) :
    pipeline_(pipeline) {
}

VideoPreview::~VideoPreview() {
    destroy();
}

GstFlowReturn VideoPreview::onPreviewFrame(GstElement* sink, gpointer user_data) {
    printf("VideoPreview::onPreviewFrame\n");

    auto self = static_cast<VideoPreview*>(user_data);
    if (!self || !self->buffer_) {
        return GST_FLOW_OK;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return GST_FLOW_OK;
    }

    self->buffer_->setPreview(sample);
    return GST_FLOW_OK;
}

static GstFlowReturn onPreviewFrameCallback(GstAppSink* sink, gpointer user_data) {
    return VideoPreview::onPreviewFrame(reinterpret_cast<GstElement*>(sink), user_data);
}

static GstPadProbeReturn debug_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    printf("BUFFER REACHED PREVIEW QUEUE!\n");
    return GST_PAD_PROBE_OK;
}

bool VideoPreview::create(GstDevice* device, std::shared_ptr<VideoPreviewBuffer>& buffer) {
    printf("VideoPreview::create\n");

    bin_ = pipeline_->createChildBin();
    if (!bin_) {
        return false;
    }

    auto queue = gst_element_factory_make("queue", NULL);
    if (!queue) {
        return false;
    }
    g_object_set(queue, "leaky", 1, "max-size-buffers", 2, nullptr);

    GstElement* videorate = gst_element_factory_make("videorate", NULL);
    if (!videorate) {
        return false;
    }

    GstElement* videoconvert = gst_element_factory_make("videoconvert", NULL);
    if (!videoconvert) {
        return false;
    }

    GstElement* videoscale = gst_element_factory_make("videoscale", NULL);
    if (!videoscale) {
        return false;
    }

    GstElement* capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!capsfilter) {
        return false;
    }

    GstCaps* caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, 320,
        "height", G_TYPE_INT, 240,
        "framerate", GST_TYPE_FRACTION, 10, 1,
        nullptr
    );

    if (!caps) {
        return false;
    }

    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    GstElement* enc = gst_element_factory_make("jpegenc", NULL);
    if (!enc) {
        return false;
    }

    auto appsink = gst_element_factory_make("appsink", NULL);
    if (!appsink) {
        return false;
    }

    g_object_set(appsink,
        "emit-signals", FALSE,
        "sync", FALSE,
        "async", FALSE,
        "drop", TRUE,
        "max-buffers", 1,
        nullptr);

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onPreviewFrameCallback;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, nullptr);

    gst_bin_add_many(GST_BIN(bin_), queue, videorate, videoconvert, videoscale, capsfilter, enc, appsink, NULL);

    if (!gst_element_link_many(queue, videorate, videoconvert, videoscale, capsfilter, enc, appsink, NULL)) {
        printf("VideoPreview::create: failed to link internal elements\n");
        return false;
    }

    ghost_pad_ = add_ghost_pad(bin_, queue, "sink", NULL);
    if (!ghost_pad_) {
        printf("VideoPreview::create: failed to add ghost pad\n");
        return false;
    }

    capture_ = pipeline_->ensureVideoCapture(device);
    if (!capture_) {
        printf("VideoPreview::create: failed to ensure video capture\n");
        return false;
    }

    if (!gst_element_sync_state_with_parent(bin_)) {
        printf("VideoPreview::create: failed to sync state with parent\n");
        return false;
    }

    auto src_pad = capture_->linkNextSrcPad(ghost_pad_);
    if (!src_pad) {
        printf("VideoPreview::create: failed to link next src pad\n");
        return false;
    }

    src_pad_ = src_pad;
    buffer_ = buffer;

    auto sinkpad = gst_element_get_static_pad(queue, "sink");
    gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_BUFFER, debug_probe, NULL, NULL);
    gst_object_unref(sinkpad);

    printf("VideoPreview::create: successfully started preview bin\n");
    return true;
}

void VideoPreview::destroy() {
    if (!bin_) {
        return;
    }

    // 1. Отцепляем входной пад от потока захвата
    if (src_pad_ && ghost_pad_) {
        gst_pad_unlink(src_pad_, ghost_pad_);
    }

    // 2. Возвращаем request-пад обратно в MediaCapture::tee
    if (capture_ && src_pad_) {
        capture_->removeSrcPad(src_pad_);
        src_pad_ = nullptr;
        capture_.reset();
    }

    // 4. Останавливаем и удаляем bin кодировщика
    gst_element_set_state(bin_, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(pipeline_->getElement()), bin_);
    bin_ = nullptr;
}
