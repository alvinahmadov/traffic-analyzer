#include <unistd.h>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "common.hpp"
#include "sinks.hpp"

static uint g_uid{};
static GstRTSPServer *g_servers[MAX_SINK_BINS];
static uint g_server_count{};
static GMutex g_server_cnt_lock;

GST_DEBUG_CATEGORY_EXTERN(NVDS_APP);

/**
 * Function to create sink bin for Display / Fakesink.
 */
static bool create_render_bin(SinkRenderConfig *config, SinkSubBin *sub_sink)
{
	bool success{};
	std::string elem_name;
	GstElement *connect_to;
	GstCaps *caps = nullptr;

	g_uid++;

	struct cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, config->gpu_id);

	elem_name = fmt::format("sink_sub_bin{}", g_uid);
	sub_sink->bin = gst::bin_new(elem_name);
	if(!sub_sink->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_sink{}", g_uid);
	switch(config->type)
	{
#ifndef IS_TEGRA
		case SinkType::RENDER_EGL:
			GST_CAT_INFO(NVDS_APP, "NVvideo renderer\n");
			sub_sink->sink = gst::element_factory_make(TADS_ELEM_SINK_EGL, elem_name);
			g_object_set(G_OBJECT(sub_sink->sink), "window-x", config->offset_x, "window-y", config->offset_y, "window-width",
									 config->width, "window-height", config->height, nullptr);
			g_object_set(G_OBJECT(sub_sink->sink), "enable-last-sample", false, nullptr);
			break;
#endif
		case SinkType::RENDER_DRM:
#ifndef IS_TEGRA
			TADS_ERR_MSG_V("nvdrmvideosink is only supported for Jetson");
			return false;
#else
			GST_CAT_INFO(NVDS_APP, "NVvideo renderer\n");
			sub_sink->sink = gst::element_factory_make(TADS_ELEM_SINK_DRM, elem_name);
			if((int)config->color_range > -1)
			{
				g_object_set(G_OBJECT(sub_sink->sink), "color-range", config->color_range, nullptr);
			}
			g_object_set(G_OBJECT(sub_sink->sink), "conn-id", config->conn_id, nullptr);
			g_object_set(G_OBJECT(sub_sink->sink), "plane-id", config->plane_id, nullptr);
			if((int)config->set_mode > -1)
			{
				g_object_set(G_OBJECT(sub_sink->sink), "set-mode", config->set_mode, nullptr);
			}
			break;
#endif
#ifdef IS_TEGRA
		case SinkType::RENDER_3D:
			GST_CAT_INFO(NVDS_APP, "NVvideo renderer\n");
			sub_sink->sink = gst::element_factory_make(TADS_ELEM_SINK_3D, elem_name);
			g_object_set(G_OBJECT(sub_sink->sink), "window-x", config->offset_x, "window-y", config->offset_y, "window-width",
									 config->width, "window-height", config->height, nullptr);
			g_object_set(G_OBJECT(sub_sink->sink), "enable-last-sample", false, nullptr);
			break;
#endif
		case SinkType::FAKE:
			sub_sink->sink = gst::element_factory_make(TADS_ELEM_SINK_FAKESINK, elem_name);
			g_object_set(G_OBJECT(sub_sink->sink), "enable-last-sample", false, nullptr);
			break;
		default:
			return false;
	}

	if(!sub_sink->sink)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(sub_sink->sink), "sync", config->sync, "max-lateness", -1, "async", false, "qos", config->qos,
							 nullptr);

	if(!prop.integrated)
	{
		elem_name = fmt::format("sink_sub_bin_cap_filter{}", g_uid);
		sub_sink->cap_filter = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
		if(!sub_sink->cap_filter)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(GST_BIN(sub_sink->bin), sub_sink->cap_filter);
	}

	elem_name = fmt::format("sink_sub_bin_transform{}", g_uid);
#ifndef IS_TEGRA
	if(config->type == SinkType::RENDER_EGL)
	{
		if(prop.integrated)
		{
			sub_sink->transform = gst::element_factory_make(TADS_ELEM_EGLTRANSFORM, elem_name);
		}
		else
		{
			sub_sink->transform = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
		}
		if(!sub_sink->transform)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(GST_BIN(sub_sink->bin), sub_sink->transform);

		if(!prop.integrated)
		{
			caps = gst_caps_new_empty_simple("video/x-raw");

			GstCapsFeatures *feature;
			feature = gst_caps_features_new(MEMORY_FEATURES.c_str(), nullptr);
			gst_caps_set_features(caps, 0, feature);
			g_object_set(G_OBJECT(sub_sink->cap_filter), "caps", caps, nullptr);

			g_object_set(G_OBJECT(sub_sink->transform), "gpu-id", config->gpu_id, nullptr);
			g_object_set(G_OBJECT(sub_sink->transform), "nvbuf-memory-type", config->nvbuf_memory_type, nullptr);
		}
	}
#endif

	elem_name = fmt::format("render_queue{}", g_uid);
	sub_sink->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!sub_sink->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add_many(GST_BIN(sub_sink->bin), sub_sink->queue, sub_sink->sink, nullptr);

	connect_to = sub_sink->sink;

	if(sub_sink->cap_filter)
	{
		TADS_LINK_ELEMENT(sub_sink->cap_filter, connect_to);
		connect_to = sub_sink->cap_filter;
	}

	if(sub_sink->transform)
	{
		TADS_LINK_ELEMENT(sub_sink->transform, connect_to);
		connect_to = sub_sink->transform;
	}

	TADS_LINK_ELEMENT(sub_sink->queue, connect_to);

	TADS_BIN_ADD_GHOST_PAD(sub_sink->bin, sub_sink->queue, "sink");

	success = true;

done:
	if(caps)
	{
		gst_caps_unref(caps);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static void broker_queue_overrun([[maybe_unused]] GstElement *sink_queue, [[maybe_unused]] void *data)
{
	TADS_WARN_MSG_V("nvmsgbroker queue overrun; Older Message Buffer "
									"Dropped; Network bandwidth might be insufficient\n");
}

/**
 * Function to create sink bin to generate meta-msg, convert to json based on
 * a schema and send over msgbroker.
 */
static bool create_msg_conv_broker_bin(SinkMsgConvBrokerConfig *config, SinkSubBin *bin)
{
	/** Create the subbin: -> q -> msgconv -> msgbroker bin */
	bool success{};
	std::string elem_name;

	g_uid++;

	elem_name = fmt::format("sink_sub_bin{}", g_uid);
	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}
	elem_name = fmt::format("sink_sub_bin_queue{}", g_uid);
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	/** set threshold on queue to avoid pipeline choke when broker is stuck on network
	 * leaky=2 (2): downstream       - Leaky on downstream (old buffers) */
	g_object_set(G_OBJECT(bin->queue), "leaky", 2, nullptr);
	g_object_set(G_OBJECT(bin->queue), "max-size-buffers", 20, nullptr);
	g_signal_connect(G_OBJECT(bin->queue), "overrun", G_CALLBACK(broker_queue_overrun), bin);

	/* Create msg converter to generate payload from buffer metadata */
	elem_name = fmt::format("sink_sub_bin_transform{}", g_uid);
	if(config->disable_msgconv)
	{
		bin->transform = gst::element_factory_make("queue", elem_name);
	}
	else
	{
		bin->transform = gst::element_factory_make(TADS_ELEM_MSG_CONV, elem_name);
	}
	if(!bin->transform)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(!config->disable_msgconv)
		g_object_set(G_OBJECT(bin->transform), "config", config->config_file_path.c_str(), "msg2p-lib",
								 (!config->conv_msg2p_lib.empty() ? config->conv_msg2p_lib.c_str() : "null"), "payload-type",
								 config->conv_payload_type, "comp-id", config->conv_comp_id, "debug-payload-dir",
								 config->debug_payload_dir.c_str(), "multiple-payloads", config->multiple_payloads, "msg2p-newapi",
								 config->conv_msg2p_new_api, "frame-interval", config->conv_frame_interval, nullptr);

	/* Create msg broker to send payload to g_servers */
	elem_name = fmt::format("sink_sub_bin_sink{}", g_uid);
	bin->sink = gst::element_factory_make(TADS_ELEM_MSG_BROKER, elem_name);
	if(!bin->sink)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}
	g_object_set(G_OBJECT(bin->sink), "proto-lib", config->proto_lib.c_str(), "conn-str", config->conn_str.c_str(),
							 "topic", config->topic.c_str(), "sync", config->sync, "async", false, "config",
							 config->broker_config_file_path.c_str(), "comp-id", config->broker_comp_id, "new-api", config->new_api,
							 nullptr);

	gst_bin_add_many(GST_BIN(bin->bin), bin->queue, bin->transform, bin->sink, nullptr);

	TADS_LINK_ELEMENT(bin->queue, bin->transform);
	TADS_LINK_ELEMENT(bin->transform, bin->sink);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

/**
 * Probe function to drop upstream "GST_QUERY_SEEKING" query from h264parse element.
 * This is a WAR to avoid memory leaks from h264parse element
 */
static GstPadProbeReturn
seek_query_drop_prob([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, [[maybe_unused]] void *data)
{
	if(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)
	{
		GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
		if(GST_QUERY_TYPE(query) == GST_QUERY_SEEKING)
		{
			return GST_PAD_PROBE_DROP;
		}
	}
	return GST_PAD_PROBE_OK;
}

/**
 * Function to create sink bin to generate encoded output.
 */
static bool create_encode_file_bin(SinkEncoderConfig *config, SinkSubBin *bin)
{
	GstCaps *caps{};
	bool success{};
	std::string elem_name;
	int probe_id;
	uint64_t bitrate{ static_cast<uint64_t>(config->bitrate) };
	uint profile{ config->profile };
	std::string output_file;
	std::string_view latency{ g_getenv("NVDS_ENABLE_LATENCY_MEASUREMENT") != nullptr
																? g_getenv("NVDS_ENABLE_LATENCY_MEASUREMENT")
																: "" };

	g_uid++;

	elem_name = fmt::format("sink_sub_bin{}", g_uid);
	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_queue{}", g_uid);
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_transform{}", g_uid);
	bin->transform = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!bin->transform)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_cap_filter{}", g_uid);
	bin->cap_filter = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
	if(!bin->cap_filter)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_encoder{}", g_uid);
	switch(config->codec)
	{
		case EncoderCodecType::H264:
			if(config->enc_type == EncoderEngineType::CPU)
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_SW, elem_name);
			}
			else
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_HW, elem_name);
				if(!bin->encoder)
				{
					TADS_INFO_MSG_V("Could not create NVENC encoder. Falling back to CPU encoder");
					bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_SW, elem_name);
					config->enc_type = EncoderEngineType::CPU;
				}
			}
			break;
		case EncoderCodecType::H265:
			if(config->enc_type == EncoderEngineType::CPU)
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_SW, elem_name);
			}
			else
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_HW, elem_name);
				if(!bin->encoder)
				{
					TADS_INFO_MSG_V("Could not create NVENC encoder. Falling back to CPU encoder");
					bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_SW, elem_name);
					config->enc_type = EncoderEngineType::CPU;
				}
			}
			break;
		case EncoderCodecType::MPEG4:
			bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_MPEG4, elem_name);
			break;
		default:
			goto done;
	}
	if(!bin->encoder)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(config->codec == EncoderCodecType::MPEG4 || config->enc_type == EncoderEngineType::CPU)
		caps = gst_caps_from_string("video/x-raw, format=I420");
	else
		caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=I420");
	g_object_set(G_OBJECT(bin->cap_filter), "caps", caps, nullptr);

	TADS_ELEM_ADD_PROBE(probe_id, bin->encoder, "sink", seek_query_drop_prob, GST_PAD_PROBE_TYPE_QUERY_UPSTREAM, bin);

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"
	probe_id = probe_id;
#pragma clang diagnostic pop

	if(config->codec == EncoderCodecType::MPEG4)
		config->enc_type = EncoderEngineType::CPU;

	struct cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, config->gpu_id);

	if(config->copy_meta == 1)
	{
		g_object_set(G_OBJECT(bin->encoder), "copy-meta", true, nullptr);
	}

	if(config->enc_type == EncoderEngineType::NVENC)
	{
		switch(config->output_io_mode)
		{
			case EncOutputIOMode::MMAP:
			default:
				g_object_set(G_OBJECT(bin->encoder), "output-io-mode", EncOutputIOMode::MMAP, nullptr);
				break;
			case EncOutputIOMode::DMABUF_IMPORT:
				g_object_set(G_OBJECT(bin->encoder), "output-io-mode", EncOutputIOMode::DMABUF_IMPORT, nullptr);
				break;
		}
	}

	if(config->enc_type == EncoderEngineType::NVENC)
	{
		g_object_set(G_OBJECT(bin->encoder), "profile", profile, nullptr);
		g_object_set(G_OBJECT(bin->encoder), "iframeinterval", config->iframeinterval, nullptr);
		g_object_set(G_OBJECT(bin->encoder), "bitrate", bitrate, nullptr);
		g_object_set(G_OBJECT(bin->encoder), "gpu-id", config->gpu_id, nullptr);
	}
	else
	{
		if(config->codec == EncoderCodecType::MPEG4)
			g_object_set(G_OBJECT(bin->encoder), "bitrate", bitrate, nullptr);
		else
		{
			// bitrate is in kbits/sec for software encoder x264enc and x265enc
			g_object_set(G_OBJECT(bin->encoder), "bitrate", bitrate / 1000, nullptr);
			g_object_set(G_OBJECT(bin->encoder), "speed-preset", config->sw_preset, nullptr);
		}
	}

	switch(config->codec)
	{
		case EncoderCodecType::H264:
			bin->codecparse = gst_element_factory_make("h264parse", "h264-parser");
			break;
		case EncoderCodecType::H265:
			bin->codecparse = gst_element_factory_make("h265parse", "h265-parser");
			break;
		case EncoderCodecType::MPEG4:
			bin->codecparse = gst_element_factory_make("mpeg4videoparse", "mpeg4-parser");
			break;
		default:
			goto done;
	}

	elem_name = fmt::format("sink_sub_bin_mux{}", g_uid);
	// disabling the mux when latency measurement logs are enabled
	if(!latency.empty())
	{
		bin->mux = gst::element_factory_make(TADS_ELEM_IDENTITY, elem_name);
	}
	else
	{
		std::string factory_name;
		switch(config->container)
		{
			case ContainerType::MP4:
				factory_name = TADS_ELEM_MUX_MP4;
				break;
			case ContainerType::MKV:
				factory_name = TADS_ELEM_MKV;
				break;
			default:
				goto done;
		}
		bin->mux = gst::element_factory_make(factory_name, elem_name);
	}

	if(!bin->mux)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_sink{}", g_uid);
	bin->sink = gst::element_factory_make(TADS_ELEM_SINK_FILE, elem_name);
	if(!bin->sink)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(!config->output_file.empty())
	{
		output_file = config->output_file;
	}
	else if(!config->output_file_path.empty())
	{
		std::string extension;

		switch(config->container)
		{
			case ContainerType::MKV:
				extension = "mkv";
				break;
			case ContainerType::MP4:
				extension = "mp4";
				break;
			default:
				TADS_ERR_MSG_V("Unknown container type");
				goto done;
		}
		auto time = g_get_real_time();
		output_file = fmt::format("{}/record_{}.{}", config->output_file_path, std::to_string(time), extension);
	}

	g_object_set(G_OBJECT(bin->sink), "location", output_file.c_str(), "sync", config->sync, "async", false, nullptr);

	g_object_set(G_OBJECT(bin->transform), "gpu-id", config->gpu_id, nullptr);
	gst_bin_add_many(GST_BIN(bin->bin), bin->queue, bin->transform, bin->codecparse, bin->cap_filter, bin->encoder,
									 bin->mux, bin->sink, nullptr);

	TADS_LINK_ELEMENT(bin->queue, bin->transform);

	TADS_LINK_ELEMENT(bin->transform, bin->cap_filter);
	TADS_LINK_ELEMENT(bin->cap_filter, bin->encoder);

	TADS_LINK_ELEMENT(bin->encoder, bin->codecparse);
	TADS_LINK_ELEMENT(bin->codecparse, bin->mux);
	TADS_LINK_ELEMENT(bin->mux, bin->sink);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	success = true;

done:
	if(caps)
	{
		gst_caps_unref(caps);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static bool
start_rtsp_streaming(uint rtsp_port_num, uint updsink_port_num, EncoderCodecType enctype, uint64_t udp_buffer_size)
{
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
	std::string udpsrc_pipeline;

	char port_num_Str[64]{};
	std::string encoder_name;

	if(enctype == EncoderCodecType::H264)
	{
		encoder_name = "H264";
	}
	else if(enctype == EncoderCodecType::H265)
	{
		encoder_name = "H265";
	}
	else
	{
		TADS_ERR_MSG_V("%s failed", __func__);
		return false;
	}

	if(udp_buffer_size == 0)
		udp_buffer_size = 512 * 1024;

	udpsrc_pipeline = fmt::format("( udpsrc name=pay0 port={} buffer-size={} caps=\"application/x-rtp, media=video, "
																"clock-rate=90000, encoding-name={}, payload=96 \" )",
																updsink_port_num, udp_buffer_size, encoder_name);

	sprintf(port_num_Str, "%d", rtsp_port_num);

	g_mutex_lock(&g_server_cnt_lock);

	g_servers[g_server_count] = gst_rtsp_server_new();
	g_object_set(g_servers[g_server_count], "service", port_num_Str, nullptr);

	mounts = gst_rtsp_server_get_mount_points(g_servers[g_server_count]);

	factory = gst_rtsp_media_factory_new();
	gst_rtsp_media_factory_set_launch(factory, udpsrc_pipeline.c_str());

	gst_rtsp_mount_points_add_factory(mounts, "/ds-test", factory);

	g_object_unref(mounts);

	gst_rtsp_server_attach(g_servers[g_server_count], nullptr);

	g_server_count++;

	g_mutex_unlock(&g_server_cnt_lock);

	g_print("\n *** Traffic Analyzer : Launched RTSP Streaming at rtsp://localhost:%d/ds-test ***\n\n", rtsp_port_num);

	return true;
}

static bool create_udpsink_bin(SinkEncoderConfig *config, SinkSubBin *bin)
{
	GstCaps *caps{};
	bool success{};
	std::string elem_name;
	std::string encode_name;
	std::string rtppay_name;
	[[maybe_unused]] int probe_id;

	// uint rtsp_port_num = g_rtsp_port_num++;
	g_uid++;

	elem_name = fmt::format("sink_sub_bin{}", g_uid);
	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_queue{}", g_uid);
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_transform{}", g_uid);
	bin->transform = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!bin->transform)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = fmt::format("sink_sub_bin_cap_filter{}", g_uid);
	bin->cap_filter = gst::element_factory_make(TADS_ELEM_CAPS_FILTER, elem_name);
	if(!bin->cap_filter)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	encode_name = fmt::format("sink_sub_bin_encoder{}", g_uid);
	rtppay_name = fmt::format("sink_sub_bin_rtppay{}", g_uid);

	switch(config->codec)
	{
		case EncoderCodecType::H264:
			bin->codecparse = gst::element_factory_make("h264parse", "h264-parser");
			g_object_set(G_OBJECT(bin->codecparse), "config-interval", -1, nullptr);
			bin->rtppay = gst::element_factory_make("rtph264pay", rtppay_name);
			if(config->enc_type == EncoderEngineType::CPU)
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_SW, encode_name);
			}
			else
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_HW, encode_name);
				if(!bin->encoder)
				{
					TADS_INFO_MSG_V("Could not create NVENC encoder. Falling back to CPU encoder");
					bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H264_SW, encode_name);
					config->enc_type = EncoderEngineType::CPU;
				}
			}
			break;
		case EncoderCodecType::H265:
			bin->codecparse = gst::element_factory_make("h265parse", "h265-parser");
			g_object_set(G_OBJECT(bin->codecparse), "config-interval", -1, nullptr);
			bin->rtppay = gst::element_factory_make("rtph265pay", rtppay_name);
			if(config->enc_type == EncoderEngineType::CPU)
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_SW, encode_name);
			}
			else
			{
				bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_HW, encode_name);
				if(!bin->encoder)
				{
					TADS_INFO_MSG_V("Could not create NVENC encoder. Falling back to CPU encoder");
					bin->encoder = gst::element_factory_make(TADS_ELEM_ENC_H265_SW, encode_name);
					config->enc_type = EncoderEngineType::CPU;
				}
			}
			break;
		default:
			goto done;
	}

	if(!bin->encoder)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", encode_name.c_str());
		goto done;
	}

	if(config->enc_type == EncoderEngineType::CPU)
		caps = gst_caps_from_string("video/x-raw, format=I420");
	else
		caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=I420");

	g_object_set(G_OBJECT(bin->cap_filter), "caps", caps, nullptr);

	TADS_ELEM_ADD_PROBE(probe_id, bin->encoder, "sink", seek_query_drop_prob, GST_PAD_PROBE_TYPE_QUERY_UPSTREAM, bin);

	if(!bin->rtppay)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", rtppay_name.c_str());
		goto done;
	}

	if(config->enc_type == EncoderEngineType::CPU)
	{
		// bitrate is in kbits/sec for software encoder x264enc and x265enc
		g_object_set(G_OBJECT(bin->encoder), "bitrate", config->bitrate / 1000, nullptr);
	}
	else
	{
		g_object_set(G_OBJECT(bin->encoder), "bitrate", config->bitrate, nullptr);
		g_object_set(G_OBJECT(bin->encoder), "profile", config->profile, nullptr);
		g_object_set(G_OBJECT(bin->encoder), "iframeinterval", config->iframeinterval, nullptr);
	}

	struct cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, config->gpu_id);

	if(prop.integrated)
	{
		if(config->enc_type == EncoderEngineType::NVENC)
		{
			g_object_set(G_OBJECT(bin->encoder), "preset-level", 1, nullptr);
			g_object_set(G_OBJECT(bin->encoder), "insert-sps-pps", 1, nullptr);
			g_object_set(G_OBJECT(bin->encoder), "gpu-id", config->gpu_id, nullptr);
		}
	}
	else
	{
		g_object_set(G_OBJECT(bin->transform), "gpu-id", config->gpu_id, nullptr);
	}

	elem_name = fmt::format("sink_sub_bin_udpsink{}", g_uid);
	bin->sink = gst::element_factory_make(TADS_ELEM_SINK_UDP, elem_name);
	if(!bin->sink)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(bin->sink), "host", "224.224.255.255", "port", config->udp_port, "async", false, "sync", 0,
							 nullptr);

	gst_bin_add_many(GST_BIN(bin->bin), bin->queue, bin->cap_filter, bin->transform, bin->encoder, bin->codecparse,
									 bin->rtppay, bin->sink, nullptr);

	TADS_LINK_ELEMENT(bin->queue, bin->transform);
	TADS_LINK_ELEMENT(bin->transform, bin->cap_filter);
	TADS_LINK_ELEMENT(bin->cap_filter, bin->encoder);
	TADS_LINK_ELEMENT(bin->encoder, bin->codecparse);
	TADS_LINK_ELEMENT(bin->codecparse, bin->rtppay);
	TADS_LINK_ELEMENT(bin->rtppay, bin->sink);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	success = start_rtsp_streaming(config->rtsp_port, config->udp_port, config->codec, config->udp_buffer_size);
	if(!success)
	{
		g_print("%s: start_rtsp_straming function failed\n", __func__);
	}

done:
	if(caps)
	{
		gst_caps_unref(caps);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

static GstRTSPFilterResult client_filter([[maybe_unused]] GstRTSPServer *server, [[maybe_unused]] GstRTSPClient *client,
																				 [[maybe_unused]] void *data)
{
	return GST_RTSP_FILTER_REMOVE;
}

bool create_sink_bin(uint num_sub_bins, std::vector<SinkSubBinConfig> &configs, SinkBin *sink, uint index)
{
	bool success{};
	std::string elem_name{ "sink" };
	SinkSubBin *sub_bin;
	SinkSubBinConfig *sub_bin_config;

	sink->bin = gst_bin_new(elem_name.c_str());
	if(!sink->bin)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "sink_bin_queue";
	sink->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!sink->queue)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(sink->bin), sink->queue);

	TADS_BIN_ADD_GHOST_PAD(sink->bin, sink->queue, "sink");

	elem_name = "sink_bin_tee";
	sink->tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!sink->tee)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(sink->bin), sink->tee);

	TADS_LINK_ELEMENT(sink->queue, sink->tee);

	g_object_set(G_OBJECT(sink->tee), "allow-not-linked", true, nullptr);

	for(uint i{}; i < num_sub_bins; i++)
	{
		sub_bin_config = &configs.at(i);
		sub_bin = &sink->sub_bins.at(i);

		if(!sub_bin_config->enable)
		{
			continue;
		}
		if(sub_bin_config->source_id != index)
		{
			continue;
		}
		if(sub_bin_config->link_to_demux)
		{
			continue;
		}
		switch(sub_bin_config->type)
		{
#ifndef IS_TEGRA
			case SinkType::RENDER_EGL:
#else
			case SinkType::RENDER_3D:
#endif
			case SinkType::RENDER_DRM:
			case SinkType::FAKE:
				sub_bin_config->render_config.type = sub_bin_config->type;
				sub_bin_config->render_config.sync = sub_bin_config->sync;
				if(!create_render_bin(&sub_bin_config->render_config, sub_bin))
					goto done;
				break;
			case SinkType::ENCODE_FILE:
				sub_bin_config->encoder_config.sync = sub_bin_config->sync;
				if(!create_encode_file_bin(&sub_bin_config->encoder_config, sub_bin))
					goto done;
				break;
			case SinkType::RTSP:
				if(!create_udpsink_bin(&sub_bin_config->encoder_config, sub_bin))
					goto done;
				break;
			case SinkType::MSG_CONV_BROKER:
				sub_bin_config->msg_conv_broker_config.sync = sub_bin_config->sync;
				if(!create_msg_conv_broker_bin(&sub_bin_config->msg_conv_broker_config, sub_bin))
					goto done;
				break;
			default:
				goto done;
		}

		if(sub_bin_config->type != SinkType::MSG_CONV_BROKER)
		{
			gst_bin_add(GST_BIN(sink->bin), sub_bin->bin);
			if(!gst::link_element_to_tee_src_pad(sink->tee, sub_bin->bin))
			{
				goto done;
			}
		}
		sink->num_bins++;
	}

	if(sink->num_bins == 0)
	{
		sub_bin = &sink->sub_bins.at(0);
		SinkRenderConfig config;
		config.type = SinkType::FAKE;
		if(!create_render_bin(&config, sub_bin))
			goto done;
		gst_bin_add(GST_BIN(sink->bin), sub_bin->bin);
		if(!gst::link_element_to_tee_src_pad(sink->tee, sub_bin->bin))
		{
			goto done;
		}
		sink->num_bins = 1;
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool create_demux_sink_bin(uint num_sub_bins, std::vector<SinkSubBinConfig> &configs, SinkBin *bin,
													 [[maybe_unused]] uint index)
{
	bool success{};
	SinkSubBin *sub_bin;
	SinkSubBinConfig *sub_bin_config;
	std::string element_name{ "sink" };

	bin->bin = gst::bin_new(element_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", element_name.c_str());
		goto done;
	}

	element_name = "sink_bin_queue";
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, element_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", element_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->queue);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	element_name = "sink_bin_tee";
	bin->tee = gst::element_factory_make(TADS_ELEM_TEE, element_name);
	if(!bin->tee)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", element_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->tee);

	TADS_LINK_ELEMENT(bin->queue, bin->tee);

	for(uint i = 0; i < num_sub_bins; i++)
	{
		sub_bin = &bin->sub_bins.at(i);
		sub_bin_config = &configs.at(i);

		if(!sub_bin_config->enable || !sub_bin_config->link_to_demux)
			continue;

		switch(sub_bin_config->type)
		{
#ifndef IS_TEGRA
			case SinkType::RENDER_EGL:
#else
			case SinkType::RENDER_3D:
#endif
			case SinkType::RENDER_DRM:
			case SinkType::FAKE:
				sub_bin_config->render_config.type = sub_bin_config->type;
				sub_bin_config->render_config.sync = sub_bin_config->sync;
				if(!create_render_bin(&sub_bin_config->render_config, sub_bin))
					goto done;
				break;
			case SinkType::ENCODE_FILE:
				sub_bin_config->encoder_config.sync = sub_bin_config->sync;
				if(!create_encode_file_bin(&sub_bin_config->encoder_config, sub_bin))
					goto done;
				break;
			case SinkType::RTSP:
				if(!create_udpsink_bin(&sub_bin_config->encoder_config, sub_bin))
					goto done;
				break;
			case SinkType::MSG_CONV_BROKER:
				sub_bin_config->msg_conv_broker_config.sync = sub_bin_config->sync;
				if(!create_msg_conv_broker_bin(&sub_bin_config->msg_conv_broker_config, sub_bin))
					goto done;
				break;
			default:
				goto done;
		}

		if(sub_bin_config->type != SinkType::MSG_CONV_BROKER)
		{
			gst_bin_add(GST_BIN(bin->bin), sub_bin->bin);
			if(!gst::link_element_to_tee_src_pad(bin->tee, sub_bin->bin))
			{
				goto done;
			}
		}
		bin->num_bins++;
	}

	if(bin->num_bins == 0)
	{
		SinkRenderConfig config;
		config.type = SinkType::FAKE;
		sub_bin = &bin->sub_bins.at(0);
		if(!create_render_bin(&config, sub_bin))
			goto done;
		gst_bin_add(GST_BIN(bin->bin), sub_bin->bin);
		if(!gst::link_element_to_tee_src_pad(bin->tee, sub_bin->bin))
		{
			goto done;
		}
		bin->num_bins = 1;
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

void destroy_sink_bin()
{
	GstRTSPMountPoints *mounts;
	GstRTSPSessionPool *pool;
	for(uint i{}; i < g_server_count; i++)
	{
		mounts = gst_rtsp_server_get_mount_points(g_servers[i]);
		gst_rtsp_mount_points_remove_factory(mounts, "/ds-test");
		g_object_unref(mounts);
		gst_rtsp_server_client_filter(g_servers[i], client_filter, nullptr);
		pool = gst_rtsp_server_get_session_pool(g_servers[i]);
		gst_rtsp_session_pool_cleanup(pool);
		g_object_unref(pool);
	}
}
