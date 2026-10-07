#include <media_utils.h>

GstPad* add_ghost_pad(GstElement* bin, GstElement* element, const gchar* src_pad_name, const gchar* ghost_pad_name) {
    auto src_pad = gst_element_get_static_pad(element, src_pad_name);
    if (!src_pad) {
        return false;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(ghost_pad_name, src_pad);
    if (!ghost_pad) {
        return false;
    }

    if (!gst_element_add_pad(bin, ghost_pad)) {
        return false;
    }

    return ghost_pad;
}

GstElement* createChildBin(GstElement* parent_bin) {
    auto child_bin = gst_bin_new(NULL);
    if (!child_bin) {
        return nullptr;
    }

    if (!gst_bin_add(GST_BIN(parent_bin), child_bin)) {
        gst_object_unref(child_bin);
        return nullptr;
    }

    return child_bin;
}