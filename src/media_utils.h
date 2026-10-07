#pragma once
#include <gst/gst.h>

GstPad* add_ghost_pad(GstElement* bin, GstElement* element, const gchar* src_pad_name, const gchar* ghost_pad_name);

GstElement* createChildBin(GstElement* parent_bin);