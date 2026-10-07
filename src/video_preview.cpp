#include <video_preview.h>
#include <media_utils.h>
#include <media_capture.h>

VideoPreview::VideoPreview(MediaPipeline* pipeline) :
    pipeline_(pipeline) {
}

VideoPreview::~VideoPreview() {
    destroy();
}

GstFlowReturn VideoPreview::onPreviewFrame(GstElement* sink, gpointer user_data) {
    auto self = static_cast<VideoPreview*>(user_data);
    if (!self || !self->buffer_) {
        return GST_FLOW_OK;
    }

    GstSample* sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_OK;
    }

    GstBuffer* gst_buffer = gst_sample_get_buffer(sample);
    if (gst_buffer) {
        GstMapInfo map;
        if (gst_buffer_map(gst_buffer, &map, GST_MAP_READ)) {
            self->buffer_->setPreview(map.data, map.size);
            gst_buffer_unmap(gst_buffer, &map);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

bool VideoPreview::create(GstDevice* device, std::shared_ptr<VideoPreviewBuffer>& buffer) {
    bin_ = pipeline_->createChildBin();
    if (!bin_) {
        return false;
    }

    auto queue = gst_element_factory_make("queue", "prev_q");
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
        "emit-signals", TRUE,
        "sync", FALSE,
        "async", FALSE,
        "drop", TRUE,
        "max-buffers", 1,
        nullptr);

    g_signal_connect(appsink, "new-sample", G_CALLBACK(VideoPreview::onPreviewFrame), this);

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

    auto src_pad = capture_->linkNextSrcPad(ghost_pad_);
    if (!src_pad) {
        printf("VideoPreview::create: failed to link next src pad\n");
        return false;
    }

    src_pad_ = src_pad;
    buffer_ = buffer;
    if (!gst_element_sync_state_with_parent(bin_)) {
        printf("VideoPreview::create: failed to sync state with parent\n");
        return false;
    }

    printf("VideoPreview::create: successfully started preview bin\n");
    return true;
}

void VideoPreview::destroy() {
    if (!bin_) {
        return;
    }

    auto probe_id = gst_pad_add_probe(
        src_pad_,
        GST_PAD_PROBE_TYPE_IDLE,
        unlink_cb, this, NULL);

    if (probe_id) {
        std::unique_lock<std::mutex> lk(mutex_);
        unlink_cv_.wait(lk, [this]() {
            return !bin_;
            });
    }

    capture_->removeSrcPad(src_pad_);
    capture_.reset();
    src_pad_ = 0;
}


GstPadProbeReturn VideoPreview::unlink_cb(GstPad* pad, GstPadProbeInfo*, gpointer user_data) {
    auto* self = static_cast<VideoPreview*>(user_data);
    self->unlink();
    return GST_PAD_PROBE_REMOVE;
}

void VideoPreview::unlink() {
    auto ret = gst_pad_unlink(src_pad_, ghost_pad_);

    gst_element_set_state(bin_, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(pipeline_->getElement()), bin_);

    {
        std::scoped_lock<std::mutex> lk(mutex_);
        bin_ = nullptr;
    }

    unlink_cv_.notify_one();
}