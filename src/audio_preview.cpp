#include <audio_preview.h>
#include <media_pipeline.h>
#include <media_capture.h>
#include <media_utils.h>
#include <gst/app/gstappsink.h>
#include <cmath>
#include <algorithm>

AudioPreview::AudioPreview(MediaPipeline* pipeline) :
    pipeline_(pipeline) {
}

AudioPreview::~AudioPreview() {
    destroy();
}

GstFlowReturn AudioPreview::onPreviewSample(GstElement* sink, gpointer user_data) {
    auto self = static_cast<AudioPreview*>(user_data);
    if (!self || !self->buffer_) {
        return GST_FLOW_OK;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        return GST_FLOW_OK;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (buffer) {
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            const int16_t* samples = reinterpret_cast<const int16_t*>(map.data);
            const size_t count = map.size / sizeof(int16_t);

            int32_t max_val = 0;
            for (size_t i = 0; i < count; ++i) {
                int32_t v = std::abs(static_cast<int32_t>(samples[i]));
                if (v > max_val) {
                    max_val = v;
                }
            }

            double peak = static_cast<double>(max_val) / 32768.0;
            if (peak > 1.0) {
                peak = 1.0;
            }

            // Convert peak [0.0, 1.0] to dB [-60.0, 0.0] for the UI
            double db = -60.0;
            if (peak > 1e-3) {
                db = 20.0 * std::log10(peak);
                if (db < -60.0) {
                    db = -60.0;
                } else if (db > 0.0) {
                    db = 0.0;
                }
            }

            self->buffer_->setLevel(db);
            gst_buffer_unmap(buffer, &map);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static GstFlowReturn onPreviewSampleCallback(GstAppSink* sink, gpointer user_data) {
    return AudioPreview::onPreviewSample(reinterpret_cast<GstElement*>(sink), user_data);
}

bool AudioPreview::create(GstDevice* device, std::shared_ptr<AudioPreviewBuffer>& buffer) {
    printf("AudioPreview::create\n");

    bin_ = pipeline_->createChildBin();
    if (!bin_) {
        return false;
    }

    auto queue = gst_element_factory_make("queue", NULL);
    if (!queue) {
        return false;
    }
    g_object_set(queue,
        "leaky", 2,
        "max-size-buffers", 10,
        "max-size-time", (guint64)0,
        "max-size-bytes", (guint)0,
        nullptr);

    auto audioconvert = gst_element_factory_make("audioconvert", NULL);
    if (!audioconvert) {
        return false;
    }

    auto capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!capsfilter) {
        return false;
    }

    GstCaps* caps = gst_caps_new_simple(
        "audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "channels", G_TYPE_INT, 1,
        nullptr
    );
    if (!caps) {
        return false;
    }
    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    auto appsink = gst_element_factory_make("appsink", NULL);
    if (!appsink) {
        return false;
    }
    g_object_set(appsink,
        "emit-signals", FALSE,
        "sync", FALSE,
        "async", FALSE,
        "drop", TRUE,
        "max-buffers", 2,
        nullptr);

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onPreviewSampleCallback;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, nullptr);

    gst_bin_add_many(GST_BIN(bin_), queue, audioconvert, capsfilter, appsink, NULL);

    if (!gst_element_link_many(queue, audioconvert, capsfilter, appsink, NULL)) {
        printf("AudioPreview::create: failed to link internal elements\n");
        return false;
    }

    ghost_pad_ = add_ghost_pad(bin_, queue, "sink", NULL);
    if (!ghost_pad_) {
        printf("AudioPreview::create: failed to add ghost pad\n");
        return false;
    }

    capture_ = pipeline_->ensureAudioCapture(device);
    if (!capture_) {
        printf("AudioPreview::create: failed to ensure audio capture\n");
        return false;
    }

    if (!gst_element_sync_state_with_parent(bin_)) {
        printf("AudioPreview::create: failed to sync state with parent\n");
        return false;
    }

    auto src_pad = capture_->linkNextSrcPad(ghost_pad_);
    if (!src_pad) {
        printf("AudioPreview::create: failed to link next src pad\n");
        return false;
    }

    src_pad_ = src_pad;
    buffer_ = buffer;

    printf("AudioPreview::create: successfully started audio preview bin\n");
    return true;
}

void AudioPreview::destroy() {
    if (!bin_) {
        return;
    }

    if (src_pad_ && ghost_pad_) {
        gst_pad_unlink(src_pad_, ghost_pad_);
    }

    if (capture_ && src_pad_) {
        capture_->removeSrcPad(src_pad_);
        src_pad_ = nullptr;
        capture_.reset();
    }

    gst_element_set_state(bin_, GST_STATE_NULL);
    if (pipeline_ && pipeline_->getElement()) {
        gst_bin_remove(GST_BIN(pipeline_->getElement()), bin_);
    }
    bin_ = nullptr;
    buffer_.reset();
}
