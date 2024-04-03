#include <algorithm>
#include <cstring>
#include <cstdio>

#include <gst/rtsp/gstrtsptransport.h>
#include <cuda_runtime_api.h>

#include <nvdsgstutils.h>
#include <gst-nvdssr.h>
#include <gst-nvevent.h>

#include "sources.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"

GST_DEBUG_CATEGORY_EXTERN(NVDS_APP);
GST_DEBUG_CATEGORY_EXTERN(APP_CFG_PARSER_CAT);

static bool install_mux_eosmonitor_probe = false;

static bool set_camera_csi_params(SourceConfig *config, SourceBin *bin)
{
	g_object_set(G_OBJECT(bin->src_elem), "sensor-id", config->camera_csi_sensor_id, nullptr);

	GST_CAT_DEBUG(NVDS_APP, "Setting csi camera params successful");

	return true;
}

static bool set_camera_v4l2_params(SourceConfig *config, SourceBin *bin)
{
	char device[64];

	g_snprintf(device, sizeof(device), "/dev/video%d", config->camera_v4l2_dev_node);
	g_object_set(G_OBJECT(bin->src_elem), "device", device, nullptr);

	GST_CAT_DEBUG(NVDS_APP, "Setting v4l2 camera params successful");

	return true;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantConditionsOC"
static bool create_camera_source_bin(SourceConfig *config, SourceBin *source)
{
	bool success{};
	std::string elem_name{ "src_elem" };
	GstCaps *caps{}, *caps1{};

	switch(config->type)
	{
		case SourceType::CAMERA_CSI:
			source->src_elem = gst::element_factory_make(TADS_ELEM_SRC_CAMERA_CSI, elem_name);
			break;
		case SourceType::CAMERA_V4L2:
			source->src_elem = gst::element_factory_make(TADS_ELEM_SRC_CAMERA_V4L2, elem_name);

			if(!source->src_elem)
				break;

			elem_name = "src_cap_filter1";
			source->cap_filter1 = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
			if(!source->cap_filter1)
			{
				TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
				goto done;
			}
			caps1 = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, config->source_width, "height", G_TYPE_INT,
																	config->source_height, "framerate", GST_TYPE_FRACTION, config->source_fps_n,
																	config->source_fps_d, nullptr);
			break;
		default:
			TADS_ERR_MSG_V("Unsupported source type");
			goto done;
	}

	if(!source->src_elem)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "src_cap_filter";
	source->cap_filter = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
	if(!source->cap_filter)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	if(!config->video_format.empty())
	{
		caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, config->video_format.c_str(), "width",
															 G_TYPE_INT, config->source_width, "height", G_TYPE_INT, config->source_height,
															 "framerate", GST_TYPE_FRACTION, config->source_fps_n, config->source_fps_d, nullptr);
	}
	else
	{
		caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT,
															 config->source_width, "height", G_TYPE_INT, config->source_height, "framerate",
															 GST_TYPE_FRACTION, config->source_fps_n, config->source_fps_d, nullptr);
	}

	if(config->type == SourceType::CAMERA_CSI)
	{
		GstCapsFeatures *feature;
		feature = gst_caps_features_new("memory:NVMM", nullptr);
		gst_caps_set_features(caps, 0, feature);
	}
	struct cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, config->gpu_id);

	if(config->type == SourceType::CAMERA_V4L2)
	{
		GstElement *nvvidconv2;
		GstCapsFeatures *feature;
		// Check based on igpu/dgpu instead of x86/aarch64
		GstElement *nvvidconv1{};
		if(!prop.integrated)
		{
			elem_name = "nvvidconv1";
			nvvidconv1 = gst::element_factory_make(TADS_ELEM_VIDEO_CONV, elem_name);
			if(!nvvidconv1)
			{
				TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
				goto done;
			}
		}

		feature = gst_caps_features_new("memory:NVMM", nullptr);
		gst_caps_set_features(caps, 0, feature);
		g_object_set(G_OBJECT(source->cap_filter), "caps", caps, nullptr);

		g_object_set(G_OBJECT(source->cap_filter1), "caps", caps1, nullptr);

		elem_name = "nvvidconv2";

		nvvidconv2 = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
		if(!nvvidconv2)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}

		g_object_set(G_OBJECT(nvvidconv2), "gpu-id", config->gpu_id, "nvbuf-memory-type", config->nvbuf_memory_type,
								 nullptr);

		if(!prop.integrated)
		{
			gst_bin_add_many(GST_BIN(source->bin), source->src_elem, source->cap_filter1, nvvidconv1, nvvidconv2,
											 source->cap_filter, nullptr);
		}
		else
		{
			gst_bin_add_many(GST_BIN(source->bin), source->src_elem, source->cap_filter1, nvvidconv2, source->cap_filter,
											 nullptr);
		}

		TADS_LINK_ELEMENT(source->src_elem, source->cap_filter1);

		if(!prop.integrated)
		{
			TADS_LINK_ELEMENT(source->cap_filter1, nvvidconv1);

			TADS_LINK_ELEMENT(nvvidconv1, nvvidconv2);
		}
		else
		{
			TADS_LINK_ELEMENT(source->cap_filter1, nvvidconv2);
		}

		TADS_LINK_ELEMENT(nvvidconv2, source->cap_filter);

		TADS_BIN_ADD_GHOST_PAD(source->bin, source->cap_filter, "src");
	}
	else
	{

		g_object_set(G_OBJECT(source->cap_filter), "caps", caps, nullptr);

		gst_bin_add_many(GST_BIN(source->bin), source->src_elem, source->cap_filter, nullptr);

		TADS_LINK_ELEMENT(source->src_elem, source->cap_filter);

		TADS_BIN_ADD_GHOST_PAD(source->bin, source->cap_filter, "src");
	}

	switch(config->type)
	{
		case SourceType::CAMERA_CSI:
			if(!set_camera_csi_params(config, source))
			{
				TADS_ERR_MSG_V("Could not set CSI camera properties");
			}
			break;
		case SourceType::CAMERA_V4L2:
			if(!set_camera_v4l2_params(config, source))
			{
				TADS_ERR_MSG_V("Could not set V4L2 camera properties");
			}
			break;
		default:
			TADS_ERR_MSG_V("Unsupported source type");
			goto done;
	}

	success = true;

	GST_CAT_DEBUG(NVDS_APP, "Created camera source bin successfully");

done:
	if(caps)
		gst_caps_unref(caps);

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static void cb_newpad([[maybe_unused]] GstElement *decodebin, GstPad *pad, void *data)
{
	GstCaps *caps = gst_pad_query_caps(pad, nullptr);
	const GstStructure *str = gst_caps_get_structure(caps, 0);
	const char *name = gst_structure_get_name(str);

	if(!strncmp(name, "video", 5))
	{
		auto *bin = reinterpret_cast<SourceBin *>(data);
		GstPad *sinkpad = gst_element_get_static_pad(bin->tee, "sink");
		if(gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK)
		{

			TADS_ERR_MSG_V("Failed to link decodebin to pipeline");
		}
		else
		{
			auto *config = static_cast<SourceConfig *>(g_object_get_data(G_OBJECT(bin->cap_filter), SRC_CONFIG_KEY.data()));

			gst_structure_get_int(str, "width", &config->source_width);
			gst_structure_get_int(str, "height", &config->source_height);
			gst_structure_get_fraction(str, "framerate", &config->source_fps_n, &config->source_fps_d);

			GST_CAT_DEBUG(NVDS_APP, "Decodebin linked to pipeline");
		}
		gst_object_unref(sinkpad);
	}
}

static void cb_sourcesetup([[maybe_unused]] GstElement *object, GstElement *arg0, void *data)
{
	auto *bin = reinterpret_cast<SourceBin *>(data);
	if(g_object_class_find_property(G_OBJECT_GET_CLASS(arg0), "latency"))
	{
		g_object_set(G_OBJECT(arg0), "latency", bin->latency, nullptr);
	}
	if(bin->udp_buffer_size && g_object_class_find_property(G_OBJECT_GET_CLASS(arg0), "udp-buffer-size"))
	{
		g_object_set(G_OBJECT(arg0), "udp-buffer-size", bin->udp_buffer_size, nullptr);
	}
}

/*
 * Function to seek the source stream to start.
 * It is required to play the stream in loop.
 */
static bool seek_decode(void *data)
{
	auto *bin = reinterpret_cast<SourceBin *>(data);
	bool success;

	gst_element_set_state(bin->bin, GST_STATE_PAUSED);

	success =
			gst_element_seek(bin->bin, 1.0, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
											 GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

	if(!success)
		GST_WARNING("Error in seeking pipeline");

	gst_element_set_state(bin->bin, GST_STATE_PLAYING);

	return false;
}

/**
 * Probe function to drop certain events to support custom
 * logic of looping of each source stream.
 */
static GstPadProbeReturn restart_stream_buf_prob([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	GstEvent *event = GST_EVENT(info->data);
	auto *bin = (SourceBin *)data;

	if((info->type & GST_PAD_PROBE_TYPE_BUFFER))
	{
		GST_BUFFER_PTS(GST_BUFFER(info->data)) += bin->prev_accumulated_base;
	}
	if((info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH))
	{
		if(GST_EVENT_TYPE(event) == GST_EVENT_EOS)
		{
			g_timeout_add(1, reinterpret_cast<GSourceFunc>(seek_decode), bin);
		}

		if(GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT)
		{
			GstSegment *segment;

			gst_event_parse_segment(event, (const GstSegment **)&segment);
			segment->base = bin->accumulated_base;
			bin->prev_accumulated_base = bin->accumulated_base;
			bin->accumulated_base += segment->stop;
		}
		switch(GST_EVENT_TYPE(event))
		{
			case GST_EVENT_EOS:
				/* QOS events from downstream sink elements cause decoder to drop
				 * frames after looping the file since the timestamps reset to 0.
				 * We should drop the QOS events since we have custom logic for
				 * looping individual sources. */
			case GST_EVENT_QOS:
			case GST_EVENT_SEGMENT:
			case GST_EVENT_FLUSH_START:
			case GST_EVENT_FLUSH_STOP:
				return GST_PAD_PROBE_DROP;
			default:
				break;
		}
	}
	return GST_PAD_PROBE_OK;
}

static void decodebin_child_added([[maybe_unused]] GstChildProxy *child_proxy, GObject *object, char *name, void *data)
{
	auto *bin = reinterpret_cast<SourceBin *>(data);

	SourceConfig *config = bin->config;
	if(g_strrstr(name, "decodebin") == name)
	{
		g_signal_connect(G_OBJECT(object), "child-added", G_CALLBACK(decodebin_child_added), data);
	}
	if((g_strrstr(name, "h264parse") == name) || (g_strrstr(name, "h265parse") == name))
	{
		g_object_set(object, "config-interval", -1, nullptr);
	}
	if(g_strrstr(name, "fakesink") == name)
	{
		g_object_set(object, "enable-last-sample", false, nullptr);
	}
	if(g_strrstr(name, "nvcuvid") == name)
	{
		g_object_set(object, "gpu-id", config->gpu_id, nullptr);

		g_object_set(G_OBJECT(object), "cuda-memory-type", config->cuda_memory_type, nullptr);

		g_object_set(object, "source-id", config->camera_id, nullptr);
		g_object_set(object, "num-decode-surfaces", config->num_decode_surfaces, nullptr);
		if(config->intra_decode)
			g_object_set(object, "Intra-decode", config->intra_decode, nullptr);
	}
	if(g_strstr_len(name, -1, "omx") == name)
	{
		if(config->intra_decode)
			g_object_set(object, "skip-frames", 2, nullptr);
		g_object_set(object, "disable-dvfs", true, nullptr);
	}
	if(g_strstr_len(name, -1, "nvjpegdec") == name)
	{
		g_object_set(object, "TrafficAnalyzer", true, nullptr);
	}
	if(g_strstr_len(name, -1, "nvv4l2decoder") == name)
	{
		if(config->low_latency_mode)
			g_object_set(object, "low-latency-mode", true, nullptr);
		if(config->intra_decode)
			g_object_set(object, "skip-frames", 2, nullptr);
#ifdef __aarch64__
		if(g_object_class_find_property(G_OBJECT_GET_CLASS(object), "enable-max-performance"))
		{
			g_object_set(object, "enable-max-performance", true, nullptr);
		}
#endif

		if(g_object_class_find_property(G_OBJECT_GET_CLASS(object), "gpu-id"))
		{
			g_object_set(object, "gpu-id", config->gpu_id, nullptr);
		}
		if(g_object_class_find_property(G_OBJECT_GET_CLASS(object), "cudadec-memtype"))
		{
			g_object_set(G_OBJECT(object), "cudadec-memtype", config->cuda_memory_type, nullptr);
		}
		g_object_set(object, "drop-frame-interval", config->drop_frame_interval, nullptr);
		g_object_set(object, "num-extra-surfaces", config->num_extra_surfaces, nullptr);

		/* Seek only if file is the source. */
		if(config->loop && g_strstr_len(config->uri.c_str(), -1, "file:/") == config->uri)
		{
			TADS_ELEM_ADD_PROBE(
					bin->src_buffer_probe, GST_ELEMENT(object), "sink", restart_stream_buf_prob,
					(GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_BOTH | GST_PAD_PROBE_TYPE_EVENT_FLUSH | GST_PAD_PROBE_TYPE_BUFFER),
					bin);
		}
	}
done:
	return;
}

static void cb_newpad2([[maybe_unused]] GstElement *decodebin, GstPad *pad, void *data)
{
	GstCaps *caps = gst_pad_query_caps(pad, nullptr);
	const GstStructure *str = gst_caps_get_structure(caps, 0);
	const char *name = gst_structure_get_name(str);

	if(!strncmp(name, "video", 5))
	{
		auto *bin = reinterpret_cast<SourceBin *>(data);
		GstPad *sinkpad = gst_element_get_static_pad(bin->cap_filter, "sink");
		if(gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK)
		{

			TADS_ERR_MSG_V("Failed to link decodebin to pipeline");
		}
		else
		{
			auto *config = static_cast<SourceConfig *>(g_object_get_data(G_OBJECT(bin->cap_filter), SRC_CONFIG_KEY.data()));

			gst_structure_get_int(str, "width", &config->source_width);
			gst_structure_get_int(str, "height", &config->source_height);
			gst_structure_get_fraction(str, "framerate", &config->source_fps_n, &config->source_fps_d);

			GST_CAT_DEBUG(NVDS_APP, "Decodebin linked to pipeline");
		}
		gst_object_unref(sinkpad);
	}
	gst_caps_unref(caps);
}

static void cb_newpad3([[maybe_unused]] GstElement *decodebin, GstPad *pad, void *data)
{
	GstCaps *caps = gst_pad_query_caps(pad, nullptr);
	const GstStructure *str = gst_caps_get_structure(caps, 0);
	const char *name = gst_structure_get_name(str);

	if(g_strrstr(name, "x-rtp"))
	{
		auto *bin = reinterpret_cast<SourceBin *>(data);
		GstPad *sinkpad = gst_element_get_static_pad(bin->depay, "sink");
		if(gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK)
		{

			TADS_ERR_MSG_V("Failed to link depay loader to rtsp src");
		}
		gst_object_unref(sinkpad);
	}
	gst_caps_unref(caps);
}

/* Returning false from this callback will make rtspsrc ignore the stream.
 * Ignore audio and add the proper depay element based on codec. */
static bool
cb_rtspsrc_select_stream([[maybe_unused]] GstElement *rtspsrc, [[maybe_unused]] uint num, GstCaps *caps, void *data)
{
	GstStructure *structure{ gst_caps_get_structure(caps, 0) };
	const char *media{ gst_structure_get_string(structure, "media") };
	std::string_view encoding_name{ gst_structure_get_string(structure, "encoding-name") };

	std::string elem_name;
	auto *bin = reinterpret_cast<SourceBin *>(data);
	bool success{};

	bool is_video = (!g_strcmp0(media, "video"));

	if(!is_video)
		return false;

	/* Create and add depay element only if it is not created yet. */
	if(!bin->depay)
	{
		elem_name = fmt::format("depay_elem{}", bin->bin_id);
		/* Add the proper depay element based on codec. */
		if(encoding_name == "H264")
		{
			bin->depay = gst::element_factory_make("rtph264depay", elem_name);
			elem_name = fmt::format("h264parse_elem{}", bin->bin_id);
			bin->parser = gst::element_factory_make("h264parse", elem_name);
		}
		else if(encoding_name == "H265")
		{
			bin->depay = gst::element_factory_make("rtph265depay", elem_name);
			elem_name = fmt::format("h265parse_elem{}", bin->bin_id);
			bin->parser = gst::element_factory_make("h265parse", elem_name);
		}
		else
		{
			TADS_WARN_MSG_V("%s not supported", encoding_name.data());
			return false;
		}

		if(!bin->depay)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			return false;
		}

		gst_bin_add_many(GST_BIN(bin->bin), bin->depay, bin->parser, nullptr);

		TADS_LINK_ELEMENT(bin->depay, bin->parser);
		TADS_LINK_ELEMENT(bin->parser, bin->tee_rtsp_pre_decode);

		if(!gst_element_sync_state_with_parent(bin->depay))
		{
			TADS_ERR_MSG_V("'%s' failed to sync state with parent", elem_name.c_str());
			return false;
		}
		gst_element_sync_state_with_parent(bin->parser);
	}

	success = true;
done:
	return success;
}

[[maybe_unused]]
void destroy_smart_record_bin(void *data)
{
	//	SourceBin *src_bin;
	auto *parent_bin = reinterpret_cast<SourceParentBin *>(data);

	g_return_if_fail(parent_bin);

	for(auto &src_bin : parent_bin->sub_bins)
	{
		if(src_bin.record_ctx != nullptr)
			NvDsSRDestroy(src_bin.record_ctx);
	}

	//	for(uint i = 0; i < pbin->num_bins; i++)
	//	{
	//		SourceBin *src_bin = &pbin->sub_bins[i];
	//		if(src_bin && src_bin->record_ctx)
	//			NvDsSRDestroy((NvDsSRContext *)src_bin->record_ctx);
	//	}
	parent_bin->num_bins = 0;
}

static void *smart_record_callback(NvDsSRRecordingInfo *info, void *)
{
	static GMutex mutex;
	FILE *logfile;
	g_return_val_if_fail(info, nullptr);

	g_mutex_lock(&mutex);
	logfile = fopen("smart_record.log", "a");
	if(logfile)
	{
		fprintf(logfile, "%d:%d:%d:%ldms:%s:%s\n", info->sessionId, info->width, info->height, info->duration,
						info->dirpath, info->filename);
		fclose(logfile);
	}
	else
	{
		g_print("Error in opeing smart record log file\n");
	}
	g_mutex_unlock(&mutex);

	return nullptr;
}

/**
 * Function called at regular interval to start and stop video recording.
 * This is dummy implementation to show the usages of smart record APIs.
 * startTime and Duration can be adjusted as per usecase.
 */
static bool smart_record_event_generator(void *data)
{
	NvDsSRSessionId sessId = 0;
	auto *src_bin = reinterpret_cast<SourceBin *>(data);
	uint startTime = 7;
	uint duration = 8;

	if(src_bin->config->smart_rec_duration >= 0)
		duration = src_bin->config->smart_rec_duration;

	if(src_bin->config->smart_rec_start_time >= 0)
		startTime = src_bin->config->smart_rec_start_time;

	if(src_bin->record_ctx && !src_bin->reconfiguring)
	{
		auto ctx = reinterpret_cast<NvDsSRContext *>(src_bin->record_ctx);
		if(ctx->recordOn)
		{
			NvDsSRStop(ctx, 0);
		}
		else
		{
			NvDsSRStart(ctx, &sessId, startTime, duration, nullptr);
		}
	}
	return true;
}

static void check_rtsp_reconnection_attempts(SourceBin *src_bin)
{
	bool remove_probe{ true };
	SourceBin *parent_sub_bin;

	for(uint i{}; i < src_bin->parent_bin->num_bins; i++)
	{
		parent_sub_bin = { &src_bin->parent_bin->sub_bins.at(i) };
		if(parent_sub_bin->config->type != SourceType::RTSP)
			continue;
		if(parent_sub_bin->have_eos &&
			 (parent_sub_bin->rtsp_reconnect_interval_sec == 0 || parent_sub_bin->rtsp_reconnect_attempts == 0))
		{
			remove_probe = false;
			break;
		}
		if(parent_sub_bin->num_rtsp_reconnects <= parent_sub_bin->rtsp_reconnect_attempts)
		{
			if(parent_sub_bin->rtsp_reconnect_interval_sec || !parent_sub_bin->have_eos)
			{
				remove_probe = false;
				break;
			}
		}
	}

	if(remove_probe)
	{
		GstElement *pipeline = GST_ELEMENT_PARENT(GST_ELEMENT_PARENT(src_bin->bin));
		TADS_ELEM_REMOVE_PROBE(src_bin->parent_bin->nvstreammux_eosmonitor_probe, src_bin->parent_bin->streammux, "src");
		GST_ELEMENT_ERROR(pipeline, STREAM, FAILED,
											("Reconnection attempts exceeded for all sources or EOS received."
											 " Stopping pipeline"),
											(nullptr));
	}
}

/**
 * Function called at regular interval to check if NvDsSourceType::NV_DS_SOURCE_RTSP type
 * source in the pipeline is down / disconnected. This function try to
 * reconnect the source by resetting that source pipeline.
 */
static bool watch_source_status(void *data)
{
	auto *src_bin = reinterpret_cast<SourceBin *>(data);
	struct timeval current_time;
	gettimeofday(&current_time, nullptr);
	static struct timeval last_reset_time_global = { 0, 0 };
	double time_diff_msec_since_last_reset = 1000.0 * (current_time.tv_sec - last_reset_time_global.tv_sec) +
																					 (current_time.tv_usec - last_reset_time_global.tv_usec) / 1000.0;

	if(src_bin->reconfiguring)
	{
		uint time_since_last_reconnect_sec = current_time.tv_sec - src_bin->last_reconnect_time.tv_sec;
		if(time_since_last_reconnect_sec >= SOURCE_RESET_INTERVAL_SEC)
		{
			if(time_diff_msec_since_last_reset > 3000)
			{
				if(src_bin->rtsp_reconnect_attempts == -1 || ++src_bin->num_rtsp_reconnects <= src_bin->rtsp_reconnect_attempts)
				{
					last_reset_time_global = current_time;
					// source is still not up, reconfigure it again.
					reset_source_pipeline(src_bin);
				}
				else
				{
					GST_ELEMENT_WARNING(src_bin->bin, STREAM, FAILED,
															("Number of RTSP reconnect attempts exceeded, stopping source: %d", src_bin->source_id),
															(nullptr));

					check_rtsp_reconnection_attempts(src_bin);

					gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_start());
					gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_stop(true));
					if(!gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_eos()))
					{
						GST_ERROR_OBJECT(src_bin->cap_filter1, "Interrupted, Reconnection event not sent");
					}
					if(gst_element_set_state(src_bin->bin, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
					{
						GST_ERROR_OBJECT(src_bin->bin, "Can't set source bin to nullptr");
					}

					return false;
				}
			}
		}
	}
	else
	{
		int time_since_last_buf_sec = 0;
		g_mutex_lock(&src_bin->bin_lock);
		if(src_bin->last_buffer_time.tv_sec != 0)
		{
			time_since_last_buf_sec = current_time.tv_sec - src_bin->last_buffer_time.tv_sec;
		}
		g_mutex_unlock(&src_bin->bin_lock);

		// Reset source bin if no buffers are received in the last
		// `rtsp_reconnect_interval_sec` seconds.
		if(src_bin->rtsp_reconnect_interval_sec > 0 && time_since_last_buf_sec >= src_bin->rtsp_reconnect_interval_sec)
		{
			if(time_diff_msec_since_last_reset > 3000)
			{
				if(src_bin->rtsp_reconnect_attempts == -1 || ++src_bin->num_rtsp_reconnects <= src_bin->rtsp_reconnect_attempts)
				{
					last_reset_time_global = current_time;

					TADS_WARN_MSG_V("No data from source %d since last %u sec. Trying reconnection", src_bin->bin_id,
													time_since_last_buf_sec);
					reset_source_pipeline(src_bin);
				}
				else
				{
					GST_ELEMENT_WARNING(src_bin->bin, STREAM, FAILED,
															("Number of RTSP reconnect attempts exceeded, stopping source: %d", src_bin->source_id),
															(nullptr));

					check_rtsp_reconnection_attempts(src_bin);

					gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_start());
					gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_stop(true));
					if(!gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_eos()))
					{
						GST_ERROR_OBJECT(src_bin->cap_filter1, "Interrupted, Reconnection event not sent");
					}
					if(gst_element_set_state(src_bin->bin, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
					{
						GST_ERROR_OBJECT(src_bin->bin, "Can't set source bin to nullptr");
					}

					return false;
				}
			}
		}
	}
	return true;
}

/**
 * Function called at regular interval when source bin is
 * changing state async. This function watches the state of
 * the source bin and sets it to PLAYING if the state of source
 * bin stops at PAUSED when changing state ASYNC.
 */
static bool watch_source_async_state_change(void *data)
{
	auto *src_bin = reinterpret_cast<SourceBin *>(data);
	GstState state = GST_STATE_NULL, pending = GST_STATE_NULL;
	GstStateChangeReturn ret;

	ret = gst_element_get_state(src_bin->bin, &state, &pending, 0);

	GST_CAT_DEBUG(NVDS_APP, "Bin %d %p: state:%s pending:%s ret:%s", src_bin->bin_id, src_bin,
								gst_element_state_get_name(state), gst_element_state_get_name(pending),
								gst_element_state_change_return_get_name(ret));

	// Bin is still changing state ASYNC. Wait for some more time.
	if(ret == GST_STATE_CHANGE_ASYNC)
		return true;

	// Bin state change failed / failed to get state
	if(ret == GST_STATE_CHANGE_FAILURE)
	{
		src_bin->async_state_watch_running = false;
		return false;
	}
	// Bin successfully changed state to PLAYING. Stop watching state
	if(state == GST_STATE_PLAYING)
	{
		src_bin->reconfiguring = false;
		src_bin->async_state_watch_running = false;
		src_bin->num_rtsp_reconnects = 0;
		return false;
	}
	// Bin has stopped ASYNC state change but has not gone into
	// PLAYING. Expliclity set state to PLAYING and keep watching
	// state
	gst_element_set_state(src_bin->bin, GST_STATE_PLAYING);

	return true;
}

/**
 * Probe function to monitor data output from rtspsrc.
 */
static GstPadProbeReturn rtspsrc_monitor_probe_func(GstPad *, GstPadProbeInfo *info, void *data)
{
	auto *bin = reinterpret_cast<SourceBin *>(data);
	if(info->type & GST_PAD_PROBE_TYPE_BUFFER)
	{
		g_mutex_lock(&bin->bin_lock);
		gettimeofday(&bin->last_buffer_time, nullptr);
		bin->have_eos = false;
		g_mutex_unlock(&bin->bin_lock);
	}
	if(info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
	{
		if(GST_EVENT_TYPE(info->data) == GST_EVENT_EOS)
		{
			bin->have_eos = true;
			check_rtsp_reconnection_attempts(bin);
		}
	}
	return GST_PAD_PROBE_OK;
}

/**
 * Probe function to drop EOS events from nvstreammux when RTSP sources
 * are being used so that app does not quit from EOS in case of RTSP
 * connection errors and tries to reconnect.
 */
static GstPadProbeReturn nvstreammux_eosmonitor_probe_func(GstPad *, GstPadProbeInfo *info, void *)
{
	if(info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
	{
		auto *event = reinterpret_cast<GstEvent *>(info->data);
		if(GST_EVENT_TYPE(event) == GST_EVENT_EOS)
			return GST_PAD_PROBE_DROP;
	}
	return GST_PAD_PROBE_OK;
}

static bool create_rtsp_src_bin(SourceConfig *config, SourceBin *source)
{
	NvDsSRContext *ctx{};
	bool success{};
	std::string elem_name;
	GstCaps *caps;
	GstCapsFeatures *feature;

	source->config = config;
	source->latency = config->latency;
	source->udp_buffer_size = config->udp_buffer_size;
	source->rtsp_reconnect_interval_sec = config->rtsp_reconnect_interval_sec;
	source->rtsp_reconnect_attempts = config->rtsp_reconnect_attempts;
	source->num_rtsp_reconnects = 0;

	elem_name = fmt::format("src_elem{}", source->bin_id);
	source->src_elem = gst::element_factory_make(TADS_ELEM_RTSPSRC, elem_name);
	if(!source->src_elem)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_signal_connect(G_OBJECT(source->src_elem), "select-stream", G_CALLBACK(cb_rtspsrc_select_stream), source);

	if(config->udp_buffer_size)
	{
		g_object_set(G_OBJECT(source->src_elem), "udp-buffer-size", config->udp_buffer_size, nullptr);
	}

	g_object_set(G_OBJECT(source->src_elem), "location", config->uri.c_str(), nullptr);
	g_object_set(G_OBJECT(source->src_elem), "latency", config->latency, nullptr);
	g_object_set(G_OBJECT(source->src_elem), "drop-on-latency", true, nullptr);
	configure_source_for_ntp_sync(source->src_elem);

	// 0x4 for TCP and 0x7 for All (UDP/UDP-MCAST/TCP)
	if((config->select_rtp_protocol == GST_RTSP_LOWER_TRANS_TCP) ||
		 (config->select_rtp_protocol ==
			(GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP)))
	{
		g_object_set(G_OBJECT(source->src_elem), "protocols", config->select_rtp_protocol, nullptr);
		GST_DEBUG_OBJECT(source->src_elem, "RTP Protocol=0x%x (0x4=TCP and 0x7=UDP,TCP,UDPMCAST)----\n",
										 config->select_rtp_protocol);
	}
	g_signal_connect(G_OBJECT(source->src_elem), "pad-added", G_CALLBACK(cb_newpad3), source);

	elem_name = fmt::format("tee_rtsp_elem{}", source->bin_id);
	source->tee_rtsp_pre_decode = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!source->tee_rtsp_pre_decode)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("tee_rtsp_post_decode_elem{}", source->bin_id);
	source->tee_rtsp_post_decode = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!source->tee_rtsp_post_decode)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(config->smart_record)
	{
		NvDsSRInitParams params{};
		params.containerType = static_cast<NvDsSRContainerType>(config->smart_rec_container);
		if(!config->file_prefix.empty())
		{
			auto format_str = fmt::format("{}_{}", config->file_prefix, config->camera_id);
			char *file_name_prefix = new char[format_str.length()];
			strcpy(file_name_prefix, format_str.c_str());
			params.fileNamePrefix = file_name_prefix;
		}
		// TODO: cpy from std::string::c_str()

		// params.dirpath = config->dir_path;
		params.dirpath = new char[config->dir_path.length()];
		memcpy(params.dirpath, config->dir_path.c_str(), config->dir_path.length() - 1);
		params.cacheSize = config->smart_rec_cache_size;
		params.defaultDuration = config->smart_rec_def_duration;
		params.callback = smart_record_callback;
		if(NvDsSRCreate(&ctx, &params) != NVDSSR_STATUS_OK)
		{
			TADS_ERR_MSG_V("Failed to create smart record bin");
			g_free(params.fileNamePrefix);
			goto done;
		}
		g_free(params.fileNamePrefix);
		gst_bin_add(GST_BIN(source->bin), ctx->recordbin);
		source->record_ctx = ctx;
	}

	elem_name = fmt::format("dec_que{}", source->bin_id);
	source->dec_que = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!source->dec_que)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(source->rtsp_reconnect_interval_sec > 0)
	{
		TADS_ELEM_ADD_PROBE(source->rtspsrc_monitor_probe, source->dec_que, "sink", rtspsrc_monitor_probe_func,
												GST_PAD_PROBE_TYPE_BUFFER, source);
		install_mux_eosmonitor_probe = true;
	}
	else
	{
		TADS_ELEM_ADD_PROBE(source->rtspsrc_monitor_probe, source->dec_que, "sink", rtspsrc_monitor_probe_func,
												GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, source);
	}

	elem_name = fmt::format("decodebin_elem{}", source->bin_id);
	source->decodebin = gst::element_factory_make(TADS_ELEM_DECODEBIN, elem_name);
	if(!source->decodebin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_signal_connect(G_OBJECT(source->decodebin), "pad-added", G_CALLBACK(cb_newpad2), source);
	g_signal_connect(G_OBJECT(source->decodebin), "child-added", G_CALLBACK(decodebin_child_added), source);

	elem_name = fmt::format("src_que{}", source->bin_id);
	source->cap_filter = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!source->cap_filter)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_mutex_init(&source->bin_lock);

	elem_name = fmt::format("nvvidconv_elem{}", source->bin_id);
	source->nvvidconv = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!source->nvvidconv)
	{
		TADS_ERR_MSG_V("Could not create element 'nvvidconv_elem'");
		goto done;
	}
	g_object_set(G_OBJECT(source->nvvidconv), "gpu-id", config->gpu_id, "nvbuf-memory-type", config->nvbuf_memory_type,
							 nullptr);
	if(!config->video_format.empty())
	{
		caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, config->video_format.c_str(), nullptr);
	}
	else
	{
		caps = gst_caps_new_empty_simple("video/x-raw");
	}
	feature = gst_caps_features_new("memory:NVMM", nullptr);
	gst_caps_set_features(caps, 0, feature);

	elem_name = "src_cap_filter_nvvidconv";
	source->cap_filter1 = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
	if(!source->cap_filter1)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(source->cap_filter1), "caps", caps, nullptr);
	gst_caps_unref(caps);
	gst_bin_add_many(GST_BIN(source->bin), source->src_elem, source->tee_rtsp_pre_decode, source->dec_que,
									 source->decodebin, source->cap_filter, source->tee_rtsp_post_decode, source->nvvidconv,
									 source->cap_filter1, nullptr);

	gst::link_element_to_tee_src_pad(source->tee_rtsp_pre_decode, source->dec_que);
	TADS_LINK_ELEMENT(source->dec_que, source->decodebin);

	if(ctx)
		gst::link_element_to_tee_src_pad(source->tee_rtsp_pre_decode, ctx->recordbin);

	TADS_LINK_ELEMENT(source->cap_filter, source->tee_rtsp_post_decode);

	gst::link_element_to_tee_src_pad(source->tee_rtsp_post_decode, source->nvvidconv);
	TADS_LINK_ELEMENT(source->nvvidconv, source->cap_filter1);
	TADS_BIN_ADD_GHOST_PAD(source->bin, source->cap_filter1, "src");

	g_timeout_add(1000, reinterpret_cast<GSourceFunc>(watch_source_status), source);

	// Enable local start / stop events in addition to the one
	// received from the g_servers.
	if(config->smart_record == 2)
	{
		if(source->config->smart_rec_interval)
			g_timeout_add(source->config->smart_rec_interval * 1000,
										reinterpret_cast<GSourceFunc>(smart_record_event_generator), source);
		else
			g_timeout_add(10000, reinterpret_cast<GSourceFunc>(smart_record_event_generator), source);
	}

	GST_CAT_DEBUG(NVDS_APP, "Decode bin created. Waiting for a new pad from decodebin to link");

	success = true;

done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static bool create_uridecode_src_bin(SourceConfig *config, SourceBin *source_bin)
{
	bool success{};
	std::string elem_name{ "src_elem" };
	GstCaps *caps;
	GstCapsFeatures *feature;
	source_bin->config = config;

	source_bin->src_elem = gst::element_factory_make(TADS_ELEM_SRC_URI, elem_name);
	if(!source_bin->src_elem)
	{
		TADS_ERR_MSG_V("Could not create element '%s'", elem_name.c_str());
		goto done;
	}

	source_bin->latency = config->latency;
	source_bin->udp_buffer_size = config->udp_buffer_size;

	if(starts_with(config->uri, "file://"))
	{
		config->live_source = false;
	}
	if(starts_with(config->uri, "rtsp://"))
	{
		configure_source_for_ntp_sync(source_bin->src_elem);
	}

	g_object_set(G_OBJECT(source_bin->src_elem), "uri", config->uri.c_str(), nullptr);
	g_signal_connect(G_OBJECT(source_bin->src_elem), "pad-added", G_CALLBACK(cb_newpad), source_bin);
	g_signal_connect(G_OBJECT(source_bin->src_elem), "child-added", G_CALLBACK(decodebin_child_added), source_bin);
	g_signal_connect(G_OBJECT(source_bin->src_elem), "source-setup", G_CALLBACK(cb_sourcesetup), source_bin);

	elem_name = "queue";
	source_bin->cap_filter = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!source_bin->cap_filter)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "nvvidconv_elem";
	source_bin->nvvidconv = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!source_bin->nvvidconv)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(source_bin->nvvidconv), "gpu-id", config->gpu_id, "nvbuf-memory-type",
							 config->nvbuf_memory_type, nullptr);
	if(!config->video_format.empty())
	{
		caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, config->video_format.c_str(), nullptr);
	}
	else
	{
		caps = gst_caps_new_empty_simple("video/x-raw");
	}
	feature = gst_caps_features_new("memory:NVMM", nullptr);
	gst_caps_set_features(caps, 0, feature);

	elem_name = "src_cap_filter_nvvidconv";
	source_bin->cap_filter1 = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
	if(!source_bin->cap_filter1)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(source_bin->cap_filter1), "caps", caps, nullptr);
	gst_caps_unref(caps);

	g_object_set_data(G_OBJECT(source_bin->cap_filter), SRC_CONFIG_KEY.data(), config);

	gst_bin_add_many(GST_BIN(source_bin->bin), source_bin->src_elem, source_bin->cap_filter, source_bin->nvvidconv,
									 source_bin->cap_filter1, nullptr);

	TADS_BIN_ADD_GHOST_PAD(source_bin->bin, source_bin->cap_filter1, "src");

	elem_name = "src_fakesink";
	source_bin->fakesink = gst::element_factory_make("fakesink", elem_name);
	if(!source_bin->fakesink)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "fakequeue";
	source_bin->fakesink_queue = gst::element_factory_make("queue", elem_name);
	if(!source_bin->fakesink_queue)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "src_tee";
	source_bin->tee = gst::element_factory_make("tee", elem_name);
	if(!source_bin->tee)
	{
		TADS_ERR_MSG_V("Could not create '%s'", elem_name.c_str());
		goto done;
	}
	gst_bin_add_many(GST_BIN(source_bin->bin), source_bin->fakesink, source_bin->tee, source_bin->fakesink_queue,
									 nullptr);

	TADS_LINK_ELEMENT(source_bin->fakesink_queue, source_bin->fakesink);
	gst::link_element_to_tee_src_pad(source_bin->tee, source_bin->cap_filter);

	TADS_LINK_ELEMENT(source_bin->cap_filter, source_bin->nvvidconv);
	TADS_LINK_ELEMENT(source_bin->nvvidconv, source_bin->cap_filter1);
	gst::link_element_to_tee_src_pad(source_bin->tee, source_bin->fakesink_queue);

	g_object_set(G_OBJECT(source_bin->fakesink), "sync", false, "async", false, nullptr);
	g_object_set(G_OBJECT(source_bin->fakesink), "enable-last-sample", false, nullptr);

	success = true;

	GST_CAT_DEBUG(NVDS_APP, "Decode bin created. Waiting for a new pad from decodebin to link");

done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

[[maybe_unused]]
bool create_source_bin(SourceConfig *config, SourceBin *source_bin)
{
	static uint bin_cnt = 0;
	std::string bin_name;

	bin_name = fmt::format("src_bin_{}", bin_cnt++);
	source_bin->bin = gst_bin_new(bin_name.c_str());
	if(!source_bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create 'src_bin'");
		return false;
	}

	switch(config->type)
	{
		case SourceType::CAMERA_V4L2:
			if(!create_camera_source_bin(config, source_bin))
			{
				return false;
			}
			break;
		case SourceType::URI:
			if(!create_uridecode_src_bin(config, source_bin))
			{
				return false;
			}
			source_bin->live_source = config->live_source;
			break;
		case SourceType::RTSP:
			if(!create_rtsp_src_bin(config, source_bin))
			{
				return false;
			}
			break;
		default:
			TADS_ERR_MSG_V("Source type not yet implemented!\n");
			return false;
	}

	GST_CAT_DEBUG(NVDS_APP, "Source bin created");

	return true;
}

bool create_multi_source_bin(uint num_sub_bins, std::vector<SourceConfig> &configs, SourceParentBin *source_parent)
{
	bool success{};
	std::string elem_name{ "multi_src_bin" };
	SourceBin *sub_bin;
	SourceConfig *config;

	source_parent->reset_thread = nullptr;

	source_parent->bin = gst::bin_new(elem_name);
	if(!source_parent->bin)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(source_parent->bin, "message-forward", true, nullptr);

	elem_name = "src_bin_muxer";
	source_parent->streammux = gst::element_factory_make(TADS_ELEM_STREAMMUX, elem_name);
	if(!source_parent->streammux)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}
	gst_bin_add(GST_BIN(source_parent->bin), source_parent->streammux);

	for(uint i{}; i < num_sub_bins; ++i)
	{
		config = &configs.at(i);

		if(!config->enable)
		{
			continue;
		}

		sub_bin = &source_parent->sub_bins.at(i);

		elem_name = fmt::format("src_sub_bin{}", i);
		sub_bin->bin = gst_bin_new(elem_name.c_str());
		if(!sub_bin->bin)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}

		sub_bin->bin_id = sub_bin->source_id = i;
		sub_bin->eos_done = true;
		sub_bin->reset_done = true;
		sub_bin->parent_bin = source_parent;
		config->live_source = true;
		source_parent->live_source = true;

		switch(config->type)
		{
			case SourceType::CAMERA_CSI:
			case SourceType::CAMERA_V4L2:
				if(!create_camera_source_bin(config, sub_bin))
				{
					return false;
				}
				break;
			case SourceType::URI:
				if(!create_uridecode_src_bin(config, sub_bin))
				{
					return false;
				}
				source_parent->live_source = config->live_source;
				break;
			case SourceType::RTSP:
				if(!create_rtsp_src_bin(config, sub_bin))
				{
					return false;
				}
				break;
			default:
				TADS_ERR_MSG_V("Source type not yet implemented!\n");
				return false;
		}

		gst_bin_add(GST_BIN(source_parent->bin), sub_bin->bin);

		if(!gst::link_element_to_streammux_sink_pad(source_parent->streammux, sub_bin->bin, static_cast<int>(i)))
		{
			TADS_ERR_MSG_V("source %d cannot be linked to mux's sink pad %p\n", i, source_parent->streammux);
			goto done;
		}

		source_parent->num_bins++;
	}
	TADS_BIN_ADD_GHOST_PAD(source_parent->bin, source_parent->streammux, "src");

	if(install_mux_eosmonitor_probe)
	{
		TADS_ELEM_ADD_PROBE(source_parent->nvstreammux_eosmonitor_probe, source_parent->streammux, "src",
												nvstreammux_eosmonitor_probe_func, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, source_parent);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static void set_properties_nvuribin(GstElement *element_, SourceConfig const *config)
{
	GstElementFactory *factory = GST_ELEMENT_GET_CLASS(element_)->elementfactory;
	if(!g_strcmp0(GST_OBJECT_NAME(factory), "nvurisrcbin"))
		g_object_set(element_, "uri", config->uri.c_str(), nullptr);
	if(config->num_extra_surfaces)
		g_object_set(element_, "num-extra-surfaces", config->num_extra_surfaces, nullptr);
	if(config->gpu_id)
		g_object_set(element_, "gpu-id", config->gpu_id, nullptr);
	g_object_set(element_, "cudadec-memtype", config->cuda_memory_type, nullptr);
	if(config->drop_frame_interval)
		g_object_set(element_, "drop-frame-interval", config->drop_frame_interval, nullptr);
	if(config->select_rtp_protocol)
		g_object_set(element_, "select-rtp-protocol", config->select_rtp_protocol, nullptr);
	if(config->loop)
		g_object_set(element_, "file-loop", config->loop, nullptr);
	if(config->smart_record)
		g_object_set(element_, "smart-record", config->smart_record, nullptr);
	if(config->smart_rec_cache_size)
		g_object_set(element_, "smart-rec-cache", config->smart_rec_cache_size, nullptr);
	if(config->smart_rec_container)
		g_object_set(element_, "smart-rec-container", config->smart_rec_container, nullptr);
	if(config->smart_rec_def_duration)
		g_object_set(element_, "smart-rec-default-duration", config->smart_rec_def_duration, nullptr);
	if(config->rtsp_reconnect_interval_sec)
		g_object_set(element_, "rtsp-reconnect-interval", config->rtsp_reconnect_interval_sec, nullptr);
	if(config->latency)
		g_object_set(element_, "latency", config->latency, nullptr);
	if(config->udp_buffer_size)
		g_object_set(element_, "udp-buffer-size", config->udp_buffer_size, nullptr);
}

bool create_nvmultiurisrcbin_bin(uint num_sub_bins, const std::vector<SourceConfig> &configs,
																 SourceParentBin *source_parent)
{
	bool success{};
	std::string elem_name{ "multiuri_src_bin" };
	uint i;
	const SourceConfig *config;

	source_parent->reset_thread = nullptr;

	source_parent->bin = gst_bin_new(elem_name.c_str());
	if(!source_parent->bin)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(source_parent->bin, "message-forward", true, nullptr);

	elem_name = "src_nvmultiurisrcbin";
	source_parent->nvmultiurisrcbin = source_parent->streammux =
			gst::element_factory_make(TADS_ELEM_NVMULTIURISRCBIN, elem_name);
	if(!source_parent->streammux)
	{
		TADS_ERR_MSG_V("Failed to create element 'src_nvmultiurisrcbin'");
		goto done;
	}
	gst_bin_add(GST_BIN(source_parent->bin), source_parent->streammux);

	/** set properties for the nvurisrcbin if atleast one uri was provided */
	for(i = 0; i < num_sub_bins; config = &configs.at(i++))
	{
		if(!config->enable)
			continue;
		set_properties_nvuribin(source_parent->nvmultiurisrcbin, config);
	}
	if(num_sub_bins == 0)
	{
		set_properties_nvuribin(source_parent->nvmultiurisrcbin, &configs.at(0));
	}

	TADS_BIN_ADD_GHOST_PAD(source_parent->bin, source_parent->streammux, "src");

	if(install_mux_eosmonitor_probe)
	{
		TADS_ELEM_ADD_PROBE(source_parent->nvstreammux_eosmonitor_probe, source_parent->streammux, "src",
												nvstreammux_eosmonitor_probe_func, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, source_parent);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool reset_source_pipeline(void *data)
{
	auto *src_bin = reinterpret_cast<SourceBin *>(data);
	GstState state{ GST_STATE_NULL }, pending{ GST_STATE_NULL };
	GstStateChangeReturn state_change_return;

	g_mutex_lock(&src_bin->bin_lock);
	gettimeofday(&src_bin->last_buffer_time, nullptr);
	gettimeofday(&src_bin->last_reconnect_time, nullptr);
	g_mutex_unlock(&src_bin->bin_lock);

	gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_start());
	gst_element_send_event(GST_ELEMENT(src_bin->cap_filter1), gst_event_new_flush_stop(true));
	if(gst_element_set_state(src_bin->bin, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
	{
		GST_ERROR_OBJECT(src_bin->bin, "Can't set source bin to nullptr");
		return false;
	}
	TADS_INFO_MSG_V("Resetting source %d", src_bin->bin_id);

	GST_CAT_INFO(NVDS_APP, "Reset source pipeline %s %p\n,", __func__, src_bin);
	if(!gst_element_sync_state_with_parent(src_bin->bin))
	{
		GST_ERROR_OBJECT(src_bin->bin, "Couldn't sync state with parent");
	}

	if(src_bin->parser != nullptr)
	{
		if(!gst_element_send_event(GST_ELEMENT(src_bin->parser), gst_nvevent_new_stream_reset(0)))
			GST_ERROR_OBJECT(src_bin->parser, "Interrupted, Reconnection event not sent");
	}

	state_change_return = gst_element_get_state(src_bin->bin, &state, &pending, 0);

	GST_CAT_DEBUG(NVDS_APP, "Bin %d %p: state:%s pending:%s success:%s", src_bin->bin_id, src_bin,
								gst_element_state_get_name(state), gst_element_state_get_name(pending),
								gst_element_state_change_return_get_name(state_change_return));

	if(state_change_return == GST_STATE_CHANGE_ASYNC || state_change_return == GST_STATE_CHANGE_NO_PREROLL)
	{
		if(!src_bin->async_state_watch_running)
			g_timeout_add(20, reinterpret_cast<GSourceFunc>(watch_source_async_state_change), src_bin);
		src_bin->async_state_watch_running = true;
		src_bin->reconfiguring = true;
	}
	else if(state_change_return == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PLAYING)
	{
		src_bin->reconfiguring = false;
	}
	return false;
}

[[maybe_unused]] [[maybe_unused]]
bool set_source_to_playing(void *data)
{
	auto *sub_bin = reinterpret_cast<SourceBin *>(data);
	if(sub_bin->reconfiguring)
	{
		gst_element_set_state(sub_bin->bin, GST_STATE_PLAYING);
		GST_CAT_INFO(NVDS_APP, "Reconfiguring %s  %p\n,", __func__, sub_bin);

		sub_bin->reconfiguring = false;
	}
	return false;
}

[[maybe_unused]] [[maybe_unused]]
void *reset_encodebin(void *data)
{
	auto *src_bin = reinterpret_cast<SourceBin *>(data);
	g_usleep(10000);
	GST_CAT_INFO(NVDS_APP, "Reset called %s %p\n,", __func__, src_bin);

	GST_CAT_INFO(NVDS_APP, "Reset setting null for sink %s %p\n,", __func__, src_bin);
	src_bin->reset_done = true;

	return nullptr;
}
#pragma clang diagnostic pop