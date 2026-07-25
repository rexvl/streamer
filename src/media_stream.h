#pragma once

struct MediaStream {
    GstElement* pipeline{nullptr};
    GstBus *bus{nullptr};
    std::unique_ptr<VideoSource> video;
    std::unique_ptr<AudioSource> audio;
    std::map<std::string, MediaOutput> outputs;

    bool create() {
        pipeline = gst_pipeline_new(nullptr);
        if (!pipeline) {
            return false;
        }

        bus = gst_element_get_bus(GST_ELEMENT(pipeline));
        if (!bus) {
            return false;
        }

        return true;
    }
    
    void start() {
        auto status =  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
        return;
    }

    bool addVideo(const VideoSettings& settings, GstDevice* device) {
        if (video || !outputs.empty()) {
            return false;
        }

        video = std::make_unique<VideoSource>(pipeline, settings);
        if (!video->create(device)) {
            return false;
        }

        return true;
    }

    bool addAudio(const AudioSettings& settings, GstDevice* device) {
        if (audio || !outputs.empty()) {
            return false;
        }

        audio = std::make_unique<AudioSource>(pipeline, settings);
        if (!audio->create(device)) {
            return false;
        }

        return true;
    }

    bool addOutput(const std::string& id, const OutputSettings& settings) {
        MediaOutput output(pipeline, settings);
        if (!output.create()) {
            return false;
        }

        if (video) {
            if (!output.addVideo(video->video_tee)) {
                return false;
            }
        }

        if (audio) {
            if (!output.addAudio(audio->audio_tee)) {
                return false;
            }
        }

        outputs.emplace(id, output);
        return true;
    }
    
    bool removeVideo() {
        return false;
    }
    
    bool removeAudio() {
        return false;
    }
    
    bool syncOutputs(std::map<std::string, OutputSettings> settings) {
        if (!video && !audio) {
            return false;
        }

        auto outputs_it = outputs.begin();
        while (outputs_it != outputs.end()) {
            auto settings_it = settings.find(outputs_it->first);
            if (settings.end() == settings_it) {
                // remove output
                return false;
            }
            settings.erase(settings_it);
            outputs_it++;
        }

        for (const auto& settings_it : settings) {
            if (!addOutput(settings_it.first, settings_it.second)) {
                return false;
            }
        }

        return true;
    }

    bool onError(GstElement* src) {
        if (video && video->video_bin == src) {
            printf("video source failed\n");
            return false;
        }

        if (audio && audio->audio_bin == src) {
            printf("audio source failed\n");
            return false;
        }

        for (auto& it : outputs) {
            auto& output = it.second;

            if (output.output_bin == src) {
                printf("output:%s failed\n", it.first.c_str());
                return false;
            }
        }

        return false;
    }

    bool ProcessMessage() {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_INFO | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (!msg) {
            return true;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
        {
            std::deque<GstElement*> stack;
            printf("!!!pipeline error!!!\n");

            GstObject* obj = GST_MESSAGE_SRC(msg);
            while (obj) {
                stack.push_front(GST_ELEMENT(obj));

                g_print("%s (%s)\n",
                    GST_OBJECT_NAME(obj),
                    G_OBJECT_TYPE_NAME(obj));

                obj = gst_object_get_parent(obj);
            }

            GError* err = NULL;
            gchar* debug = NULL;

            gst_message_parse_error(msg, &err, &debug);

            g_printerr("Error from %s: %s\n",
                GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)),
                err->message);

            g_error_free(err);
            g_free(debug);

            if (stack.size() < 2) {
                return false;
            }

            return onError(stack[1]);
        }

        case GST_MESSAGE_EOS:
            gst_message_unref(msg);
            return false;

        case GST_MESSAGE_STATE_CHANGED:
            {
                GstState old_state;
                GstState new_state;
                GstState pending;

                gst_message_parse_state_changed(
                    msg,
                    &old_state,
                    &new_state,
                    &pending);

                GstObject* obj = GST_MESSAGE_SRC(msg);
                g_print("%s (%s): %s -> %s (pending: %s)\n",
                    GST_OBJECT_NAME(obj),
                    G_OBJECT_TYPE_NAME(obj),
                    gst_element_state_get_name(old_state),
                    gst_element_state_get_name(new_state),
                    gst_element_state_get_name(pending));

                break;
            }

        default:
            break;
        }

        gst_message_unref(msg);
        return true;
    }


    ~MediaStream() {
        video.reset();
        audio.reset();
        outputs.clear();

        if (bus) {
            gst_object_unref(bus);
        }

        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
            gst_object_unref(pipeline);
        }

    }
};
