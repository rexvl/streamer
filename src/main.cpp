#include <gst/gst.h>


int main() {

	gst_init(nullptr, nullptr);

    GstDeviceMonitor* monitor = gst_device_monitor_new();

    // Only video input devices
    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);

    gst_device_monitor_start(monitor);

    GList* devices = gst_device_monitor_get_devices(monitor);

    for (GList* l = devices; l != NULL; l = l->next) {
        GstDevice* device = GST_DEVICE(l->data);

        g_print("Name: %s\n", gst_device_get_display_name(device));

        GstStructure* props = gst_device_get_properties(device);
        if (props) {
            gchar* str = gst_structure_to_string(props);
            g_print("Properties: %s\n", str);
            g_free(str);
        }

        g_print("\n");

        gst_object_unref(device);
    }

    g_list_free(devices);

    gst_device_monitor_stop(monitor);
    g_object_unref(monitor);

	return 0;
}