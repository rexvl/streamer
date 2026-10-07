#include <gst/gst.h>
#include <media_capture.h>
#include <media_pipeline.h>

MediaCapture::MediaCapture(MediaPipeline* pipeline) :
    pipeline_(pipeline) {
}

MediaCapture::~MediaCapture() {
    if (bin_) {
        gst_element_set_state(bin_, GST_STATE_NULL);
        if (pipeline_ && pipeline_->getElement()) {
            gst_bin_remove(GST_BIN(pipeline_->getElement()), bin_);
        }
        bin_ = nullptr;
    }
}

bool MediaCapture::create(GstDevice* device) {
    bin_ = pipeline_->createChildBin();
    if (!bin_) {
        return false;
    }

    auto capture = gst_device_create_element(device, NULL);
    if (!capture) {
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

    gst_bin_add_many(GST_BIN(bin_), capture, tee_, fakesink, NULL);

    if (!gst_element_link_many(capture, tee_, fakesink, NULL)) {
        return false;
    }

    return gst_element_sync_state_with_parent(bin_);
}

GstPad* MediaCapture::linkNextSrcPad(GstPad* other_pad) {
    auto src_pad = getNextSrcPad();
    if (!src_pad) {
        printf("MediaCapture::linkNextSrcPad: failed to get next src pad\n");
        return nullptr;
    }

    // link capture's src to stream's sink
    GstPadLinkReturn ret = gst_pad_link(src_pad, other_pad);
    if (GST_PAD_LINK_OK != ret) {
        printf("MediaCapture::linkNextSrcPad: gst_pad_link failed: %d\n", ret);
        removeSrcPad(src_pad);
        return nullptr;
    }

    return src_pad;
}

GstPad* MediaCapture::getNextSrcPad() {
    auto src_pad = gst_element_request_pad_simple(tee_, "src_%u");
    if (!src_pad) {
        return nullptr;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(NULL, src_pad);
    if (!ghost_pad) {
        return nullptr;
    }

    if (!gst_element_add_pad(bin_, ghost_pad)) {
        return nullptr;
    }

    return ghost_pad;
}

void MediaCapture::removeSrcPad(GstPad* ghost_pad) {
    GstPad* src_pad = gst_ghost_pad_get_target(GST_GHOST_PAD(ghost_pad));
    gst_element_remove_pad(bin_, ghost_pad);
    gst_element_release_request_pad(tee_, src_pad);
    gst_object_unref(src_pad);
}
