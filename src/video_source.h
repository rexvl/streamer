#pragma once

#include <atomic>
#include <cstdint>
#include <config_manager.h>
#include <preview_state.h>

class VideoSource {
    const GstElement* pipeline_;
    VideoSettings settings_; 
    std::shared_ptr<PreviewState> preview_;
    GstElement* video_bin_{ nullptr };
    GstElement* video_tee_{ nullptr };
    std::atomic<uint32_t> frame_count_{ 0 };
    uint64_t preview_index_{0};

    GstElement* capture_tee_{ nullptr };
    GstElement* preview_bin_{ nullptr };
    GstPad* preview_tee_pad_{ nullptr };
    GstElement* fakesink_{ nullptr };

    std::atomic<bool> preview_starting_{false};
    std::atomic<bool> preview_stopping_{ false };

    bool preview_enabled_{ false };

    GstElement* create_preview();

    static GstPadProbeReturn process_jpeg(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn start_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static GstPadProbeReturn stop_preview_idle(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

    bool add_preview_branch();
    void remove_preview_branch();

    void setVideoPreview(const uint8_t* buffer, const size_t buffer_size);

    void startVideoPreview();
    void stopVideoPreview();

    static GstElement* createEncoder(const VideoSettings& settings);
public:
    VideoSource(const GstElement* p, const VideoSettings& settings, const std::shared_ptr<PreviewState>& preview);
    ~VideoSource();
    bool create();
    bool update(const VideoSettings& settings);

    void syncPreview();
    bool operator==(GstElement* other) const;

    uint32_t consumeFrameCount();
    GstElement* get_tee();
};
