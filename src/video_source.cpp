#include <iostream>
#include <vector>
#include <gst/gst.h>
#include <video_source.h>
#include <media_utils.h>

VideoSource::VideoSource(GstElement* stream_bin, const VideoSettings& settings) :
    stream_bin_(stream_bin),
    settings_(settings) {
    printf("VideoSource::VideoSource\n");
}

VideoSource::~VideoSource() {
    destroy();
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

bool VideoSource::create(std::shared_ptr<MediaCapture>& capture) {
    video_bin_ = createChildBin(stream_bin_);
    if (!video_bin_) {
        return false;
    }

    auto queue = gst_element_factory_make("queue", NULL);
    if (!queue) {
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

    tee_ = gst_element_factory_make("tee", NULL);
    if (!tee_) {
        return false;
    }

    // add dummy sink to avoid getting EOS on output issue
    auto fakesink = gst_element_factory_make("fakesink", NULL);
    if (!fakesink) {
        return false;
    }

    g_object_set(fakesink,
        "sync", FALSE,
        "async", FALSE,
        nullptr);

    gst_bin_add_many(GST_BIN(video_bin_), queue, videorate, videoscale, videoconvert, capsfilter, enc, parser, tee_, fakesink, NULL);

    if (!gst_element_link_many(queue, videorate, videoscale, videoconvert, capsfilter, enc, parser, tee_, fakesink, NULL)) {
        return false;
    }

    auto sink_pad = add_ghost_pad(video_bin_, queue, "sink", NULL);
    if (!sink_pad) {
        return false;
    }

    // link encoder's sink to stream's sink
    ghost_pad_ = gst_ghost_pad_new(NULL, sink_pad);
    if (!ghost_pad_) {
        return false;
    }

    if (!gst_element_add_pad(stream_bin_, ghost_pad_)) {
        return false;
    }

    auto src_pad = capture->linkNextSrcPad(ghost_pad_);
    if (!src_pad) {
        return false;
    }

    capture_ = capture;
    src_pad_ = src_pad;
    return true;
}

GstPad* VideoSource::getNextSrcPad() {
    auto src_pad = gst_element_request_pad_simple(tee_, "src_%u");
    if (!src_pad) {
        return nullptr;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(NULL, src_pad);
    if (!ghost_pad) {
        return false;
    }

    if (!gst_element_add_pad(video_bin_, ghost_pad)) {
        return false;
    }

    return ghost_pad;
}

void VideoSource::removeSrcPad(GstPad* ghost_pad) {
    auto src_pad = gst_ghost_pad_get_target(GST_GHOST_PAD(ghost_pad));
    gst_element_remove_pad(video_bin_, ghost_pad);
    gst_element_release_request_pad(tee_, src_pad);
    gst_object_unref(src_pad);
}

bool VideoSource::update(const VideoSettings& settings) {
    bool ret = (settings_ == settings);

    if (!ret) {
        printf("!!!video settings changed!!!\n");
    }

    return ret;
}

void VideoSource::destroy() {
    if (!video_bin_) {
        return;
    }

   auto probe_id = gst_pad_add_probe(
        src_pad_,
        GST_PAD_PROBE_TYPE_IDLE,
        unlink_cb, this, NULL);

    if (probe_id) {
        std::unique_lock<std::mutex> lk(mutex_);
        unlink_cv_.wait(lk, [this]() {
            return !video_bin_;
        });
    }

    capture_->removeSrcPad(src_pad_);
    capture_.reset();
    src_pad_ = 0;
}


GstPadProbeReturn VideoSource::unlink_cb(GstPad* pad, GstPadProbeInfo*, gpointer user_data) {
    auto* self = static_cast<VideoSource*>(user_data);
    self->unlink();
    return GST_PAD_PROBE_REMOVE;
}

void VideoSource::unlink() {
    auto ret = gst_pad_unlink(src_pad_, ghost_pad_);

    gst_element_set_state(video_bin_, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(stream_bin_), video_bin_);

    {
        std::scoped_lock<std::mutex> lk(mutex_);
        video_bin_ = nullptr;
    }

    unlink_cv_.notify_one();
}