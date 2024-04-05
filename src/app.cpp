#include <cstring>
#include <cmath>

#include <fstream>

#include <nvds_tracker_meta.h>
#include <nvds_analytics_meta.h>
#include <sstream>

#include "app.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
static uint demux_batch_num = 0;

GST_DEBUG_CATEGORY_EXTERN(NVDS_APP);

[[maybe_unused]] GQuark g_dsmeta_quark;

/**
 * @brief  Add the (nvmsgconv->nvmsgbroker) sink-bin to the
 *         overall DS pipeline (if any configured) and link the same to
 *         common_elements.tee (This tee connects
 *         the common analytics path to Tiler/display-sink and
 *         to configured broker sink if any)
 *         NOTE: This API shall return true if there are no
 *         broker sinks to add to pipeline
 *
 * @param  app_ctx [IN]
 * @return true if succussful; false otherwise
 */
static bool add_and_link_broker_sink(AppContext *app_ctx);

/**
 * @brief  Checks if there are any [sink] groups
 *         configured for source_id=provided source_id
 *         NOTE: source_id key and this API is valid only when we
 *         disable [tiler] and thus use demuxer for individual
 *         stream out
 * @param  config [IN] The DS Pipeline configuration struct
 * @param  source_id [IN] Source ID for which a specific [sink]
 *         group is searched for
 */
static bool is_sink_available_for_source_id(AppConfig *config, uint source_id);

static NvDsSensorInfo *s_sensor_info_create(NvDsSensorInfo *sensor_info);
static void s_sensor_info_destroy(NvDsSensorInfo *sensor_info);

static NvDsSensorInfo *s_sensor_info_create(NvDsSensorInfo *sensor_info)
{
	auto *p_sensor_info = new NvDsSensorInfo;
	*p_sensor_info = *sensor_info;
	p_sensor_info->sensor_id = (char const *)g_strdup(sensor_info->sensor_id);
#if NVDS_VERSION_MINOR >= 4
	p_sensor_info->sensor_name = (char const *)g_strdup(sensor_info->sensor_name);
	p_sensor_info->uri = (char const *)g_strdup(sensor_info->uri);
#endif
	return p_sensor_info;
}

static void s_sensor_info_destroy(NvDsSensorInfo *sensor_info)
{
	if(!sensor_info)
		return;
	if(sensor_info->sensor_id)
	{
		g_free((void *)sensor_info->sensor_id);
	}
#if NVDS_VERSION_MINOR >= 4
	if(sensor_info->sensor_name)
	{
		g_free((void *)sensor_info->sensor_name);
	}
#endif

	g_free(sensor_info);
}

static void s_sensor_info_callback_stream_added(AppContext *app_ctx, NvDsSensorInfo *sensor_info)
{
	NvDsSensorInfo *sensor_info_from_hash = s_sensor_info_create(sensor_info);
	/** save the sensor info into the hash map */
	std::string source_str = std::to_string(sensor_info->source_id);
	g_hash_table_insert(app_ctx->sensor_info_hash, (char *)source_str.c_str(), sensor_info_from_hash);
}

static void s_sensor_info_callback_stream_removed(AppContext *app_ctx, NvDsSensorInfo *sensor_info)
{
	std::string source_str = std::to_string(sensor_info->source_id);
	NvDsSensorInfo *sensor_info_from_hash = get_sensor_info(app_ctx, sensor_info->source_id);
	/** remove the sensor info from the hash map */
	if(sensor_info_from_hash)
	{
		g_hash_table_remove(app_ctx->sensor_info_hash, (char *)source_str.c_str());
		s_sensor_info_destroy(sensor_info_from_hash);
	}
}

NvDsSensorInfo *get_sensor_info(AppContext *app_ctx, uint source_id)
{
	std::string source_str = std::to_string(source_id);
	auto *sensor_info = static_cast<NvDsSensorInfo *>(g_hash_table_lookup(app_ctx->sensor_info_hash, source_str.c_str()));
	return sensor_info;
}

/*Note: Below callbacks/functions defined for FPS logging,
 *  when nvmultiurisrcbin is being used*/
static FPSSensorInfo *s_fps_sensor_info_create(FPSSensorInfo *sensor_info);
FPSSensorInfo *get_fps_sensor_info(AppContext *app_ctx, uint source_id);
static void s_fps_sensor_info_destroy(FPSSensorInfo *sensor_info);

static FPSSensorInfo *s_fps_sensor_info_create(FPSSensorInfo *sensor_info)
{
	auto *fps_sensor_info = new FPSSensorInfo;
	*fps_sensor_info = *sensor_info;
	fps_sensor_info->uri = (char const *)g_strdup(sensor_info->uri);
	fps_sensor_info->source_id = sensor_info->source_id;
	fps_sensor_info->sensor_id = (char const *)g_strdup(sensor_info->sensor_id);
	fps_sensor_info->sensor_name = (char const *)g_strdup(sensor_info->sensor_name);
	return fps_sensor_info;
}

static void s_fps_sensor_info_destroy(FPSSensorInfo *sensor_info)
{
	if(!sensor_info)
		return;
	if(sensor_info->sensor_id)
	{
		g_free((void *)sensor_info->sensor_id);
	}
	if(sensor_info->sensor_name)
	{
		g_free((void *)sensor_info->sensor_name);
	}
	if(sensor_info->uri)
	{
		g_free((void *)sensor_info->uri);
	}

	g_free(sensor_info);
}

static void s_fps_sensor_info_callback_stream_added(AppContext *app_ctx, FPSSensorInfo *fps_sensor_info)
{

	FPSSensorInfo *fps_sensor_info_to_hash = s_fps_sensor_info_create(fps_sensor_info);
	/** save the sensor info into the hash map */
	g_hash_table_insert(app_ctx->perf_struct.fps_info_hash, GUINT_TO_POINTER(fps_sensor_info->source_id),
											fps_sensor_info_to_hash);
}

FPSSensorInfo *get_fps_sensor_info(AppContext *app_ctx, uint source_id)
{
	auto *fps_sensor_info = static_cast<FPSSensorInfo *>(
			g_hash_table_lookup(app_ctx->perf_struct.fps_info_hash, GUINT_TO_POINTER(source_id)));
	return fps_sensor_info;
}

static void s_fps_sensor_info_callback_stream_removed(AppContext *app_ctx, FPSSensorInfo *fps_sensor_info)
{

	FPSSensorInfo *fps_ensor_info_from_hash = get_fps_sensor_info(app_ctx, fps_sensor_info->source_id);
	/** remove the sensor info from the hash map */
	if(fps_ensor_info_from_hash)
	{
		g_hash_table_remove(app_ctx->perf_struct.fps_info_hash, GUINT_TO_POINTER(fps_sensor_info->source_id));
		s_fps_sensor_info_destroy(fps_ensor_info_from_hash);
	}
}

/**
 * callback function to receive messages from components
 * in the pipeline.
 */
static bool bus_callback(GstBus *, GstMessage *message, void *data)
{
	auto *app_ctx = reinterpret_cast<AppContext *>(data);
	GST_CAT_DEBUG(NVDS_APP, "Received message on bus: source %s, msg_type %s", GST_MESSAGE_SRC_NAME(message),
								GST_MESSAGE_TYPE_NAME(message));
	switch(GST_MESSAGE_TYPE(message))
	{
		case GST_MESSAGE_INFO:
		{
			GError *error{};
			char *debuginfo{};
			gst_message_parse_info(message, &error, &debuginfo);
			TADS_INFO_MSG_V("%s: %s", GST_OBJECT_NAME(message->src), error->message);
			if(debuginfo)
			{
				TADS_DBG_MSG_V("%s", debuginfo);
			}
			g_error_free(error);
			g_free(debuginfo);
			break;
		}
		case GST_MESSAGE_WARNING:
		{
			GError *error{};
			char *debuginfo{};
			gst_message_parse_warning(message, &error, &debuginfo);
			TADS_WARN_MSG_V("%s: %s", GST_OBJECT_NAME(message->src), error->message);
			if(debuginfo)
			{
				TADS_DBG_MSG_V("%s", debuginfo);
			}
			g_error_free(error);
			g_free(debuginfo);
			break;
		}
		case GST_MESSAGE_ERROR:
		{
			GError *error{};
			char *debug_info{};
			const char *attempts_error = "Reconnection attempts exceeded for all sources or EOS received.";
			uint i = 0;
			gst_message_parse_error(message, &error, &debug_info);

			if(strstr(error->message, attempts_error))
			{
				TADS_ERR_MSG_V("Reconnection attempt  exceeded or EOS received for all sources.\nExiting.");
				g_error_free(error);
				g_free(debug_info);
				app_ctx->status = 0;
				app_ctx->quit = true;
				return true;
			}

			TADS_ERR_MSG_V("%s: %s", GST_OBJECT_NAME(message->src), error->message);
			if(debug_info)
			{
				TADS_DBG_MSG_V("%s", debug_info);
			}

			SourceParentBin *source_parent_bin = &app_ctx->pipeline.multi_src_bin;
			auto *msg_src_elem = reinterpret_cast<GstElement *>(GST_MESSAGE_SRC(message));
			bool bin_found{};
			/* Find the source bin which generated the error. */
			while(msg_src_elem && !bin_found)
			{
				for(i = 0; i < source_parent_bin->num_bins; i++)
				{
					auto *src_bin{ &source_parent_bin->sub_bins.at(i) };
					if(src_bin->src_elem == msg_src_elem || src_bin->bin == msg_src_elem)
					{
						bin_found = true;
						break;
					}
				}
				msg_src_elem = GST_ELEMENT_PARENT(msg_src_elem);
			}

			if((i != source_parent_bin->num_bins) && (app_ctx->config.multi_source_configs[0].type == SourceType::RTSP))
			{
				// Error from one of RTSP source.
				SourceBin *sub_bin{ &source_parent_bin->sub_bins.at(i) };

				if(!sub_bin->reconfiguring || g_strrstr(debug_info, "500 (Internal Server Error)"))
				{
					sub_bin->reconfiguring = true;
					g_timeout_add(0, reinterpret_cast<GSourceFunc>(reset_source_pipeline), sub_bin);
				}
				g_error_free(error);
				g_free(debug_info);
				return true;
			}

			if(app_ctx->config.multi_source_configs[0].type == SourceType::CAMERA_V4L2)
			{
				if(g_strrstr(debug_info, "reason not-negotiated (-4)"))
				{
					TADS_INFO_MSG_V("incorrect camera parameters provided, please provide supported resolution and frame rate\n");
				}

				if(g_strrstr(debug_info, "Buffer pool activation failed"))
				{
					TADS_INFO_MSG_V("usb bandwidth might be saturated\n");
				}
			}

			g_error_free(error);
			g_free(debug_info);
			app_ctx->status = -1;
			app_ctx->quit = true;
			break;
		}
		case GST_MESSAGE_STATE_CHANGED:
		{
			GstState oldstate, newstate;
			gst_message_parse_state_changed(message, &oldstate, &newstate, nullptr);
			if(GST_ELEMENT(GST_MESSAGE_SRC(message)) == app_ctx->pipeline.pipeline)
			{
				switch(newstate)
				{
					case GST_STATE_PLAYING:
						TADS_INFO_MSG_V("Pipeline running");
						GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(app_ctx->pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
																							"ds-app-playing");
						break;
					case GST_STATE_PAUSED:
						if(oldstate == GST_STATE_PLAYING)
						{
							TADS_INFO_MSG_V("Pipeline paused");
						}
						break;
					case GST_STATE_READY:
						GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(app_ctx->pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
																							"ds-app-ready");
						if(oldstate == GST_STATE_NULL)
						{
							TADS_INFO_MSG_V("Pipeline ready");
						}
						else
						{
							TADS_INFO_MSG_V("Pipeline stopped");
						}
						break;
					case GST_STATE_NULL:
						g_mutex_lock(&app_ctx->app_lock);
						g_cond_broadcast(&app_ctx->app_cond);
						g_mutex_unlock(&app_ctx->app_lock);
						break;
					default:
						break;
				}
			}
			break;
		}
		case GST_MESSAGE_EOS:
		{
			/*
			 * In normal scenario, this would use g_main_loop_quit() to exit the
			 * loop and release the resources. Since this application might be
			 * running multiple pipelines through configuration files, it should wait
			 * till all pipelines are done.
			 */
			TADS_INFO_MSG_V("Received EOS. Exiting ...");
			app_ctx->quit = true;
			return false;
		}
		case GST_MESSAGE_ELEMENT:
		{
			if(gst_nvmessage_is_stream_add(message))
			{
				g_mutex_lock(&(app_ctx->perf_struct).struct_lock);

				NvDsSensorInfo sensor_info = { 0 };
				const char *sensor_name;
#if NVDS_VERSION_MINOR >= 4
				sensor_name = sensor_info.sensor_name;
#else
				sensor_name = nullptr;
#endif

				gst_nvmessage_parse_stream_add(message, &sensor_info);
				TADS_INFO_MSG_V("new stream added [%d:%s:%s]", sensor_info.source_id, sensor_info.sensor_id, sensor_name);
				/** Callback */
				s_sensor_info_callback_stream_added(app_ctx, &sensor_info);
				GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(app_ctx->pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
																					"ds-app-added");
				FPSSensorInfo fpssensor_info = { 0 };
#if NVDS_VERSION_MINOR >= 4
				gst_nvmessage_parse_fps_stream_add(message, reinterpret_cast<NvDsSensorInfo *>(&fpssensor_info));
#else
				gst_nvmessage_parse_stream_add(message, reinterpret_cast<NvDsSensorInfo *>(&fpssensor_info));
#endif
				s_fps_sensor_info_callback_stream_added(app_ctx, &fpssensor_info);

				g_mutex_unlock(&(app_ctx->perf_struct).struct_lock);
			}
			if(gst_nvmessage_is_stream_remove(message))
			{
				g_mutex_lock(&(app_ctx->perf_struct).struct_lock);
				NvDsSensorInfo sensor_info = { 0 };
				gst_nvmessage_parse_stream_remove(message, &sensor_info);
				g_print("new stream removed [%d:%s]\n\n\n\n", sensor_info.source_id, sensor_info.sensor_id);
				GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(app_ctx->pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
																					"ds-app-removed");
				/** Callback */
				s_sensor_info_callback_stream_removed(app_ctx, &sensor_info);
				FPSSensorInfo fpssensor_info = { 0 };

#if NVDS_VERSION_MINOR >= 4
				gst_nvmessage_parse_fps_stream_remove(message, reinterpret_cast<NvDsSensorInfo *>(&fpssensor_info));
#else
				gst_nvmessage_parse_stream_remove(message, reinterpret_cast<NvDsSensorInfo *>(&fpssensor_info));
#endif
				s_fps_sensor_info_callback_stream_removed(app_ctx, &fpssensor_info);
				g_mutex_unlock(&(app_ctx->perf_struct).struct_lock);
			}
			break;
		}
		default:
			break;
	}
	return true;
}

static int component_id_compare_func(gconstpointer a, gconstpointer b)
{
	const auto *cmetaa = reinterpret_cast<const NvDsClassifierMeta *>(a);
	const auto *cmetab = reinterpret_cast<const NvDsClassifierMeta *>(b);

	if(cmetaa->unique_component_id < cmetab->unique_component_id)
		return -1;
	if(cmetaa->unique_component_id > cmetab->unique_component_id)
		return 1;
	return 0;
}

/**
 * Function to process the attached metadata. This is just for demonstration
 * and can be removed if not required.
 * Here it demonstrates to use bounding boxes of different color and size for
 * different type / class of objects.
 * It also demonstrates how to join the different labels(PGIE + SGIEs)
 * of an object to form a single string.
 */
[[maybe_unused]]
static void process_meta(AppContext *app_ctx, NvDsBatchMeta *batch_meta)
{
	// For single source always display text either with demuxer or with tiler
	if(app_ctx->config.tiled_display_config.enable == TiledDisplayState::DISABLED ||
		 app_ctx->config.num_source_sub_bins == 1)
	{
		app_ctx->show_bbox_text = true;
	}

	for(NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next)
	{
		auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);
		for(NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next)
		{
			auto *obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			int class_index = obj_meta->class_id;
			auto class_str = std::to_string(class_index);
			GieConfig *gie_config = nullptr;
			char *str_ins_pos;

			if(obj_meta->unique_component_id == (int)app_ctx->config.primary_gie_config.unique_id)
			{
				gie_config = &app_ctx->config.primary_gie_config;
			}
			else
			{
				for(int i = 0; i < (int)app_ctx->config.num_secondary_gie_sub_bins; i++)
				{
					gie_config = &app_ctx->config.secondary_gie_sub_bin_configs[i];
					if(obj_meta->unique_component_id == (int)gie_config->unique_id)
					{
						break;
					}
					gie_config = nullptr;
				}
			}
			g_free(obj_meta->text_params.display_text);
			obj_meta->text_params.display_text = nullptr;

			if(gie_config != nullptr)
			{
				if(g_hash_table_contains(gie_config->bbox_border_color_table, std::to_string(class_index).c_str()))
				{
					obj_meta->rect_params.border_color = *static_cast<NvOSD_ColorParams *>(
							g_hash_table_lookup(gie_config->bbox_border_color_table, class_str.c_str()));
				}
				else
				{
					obj_meta->rect_params.border_color = gie_config->bbox_border_color;
				}
				obj_meta->rect_params.border_width = app_ctx->config.osd_config.border_width;

				if(g_hash_table_contains(gie_config->bbox_bg_color_table, class_str.c_str()))
				{
					obj_meta->rect_params.has_bg_color = 1;
					obj_meta->rect_params.bg_color = *static_cast<NvOSD_ColorParams *>(
							g_hash_table_lookup(gie_config->bbox_bg_color_table, class_str.c_str()));
				}
				else
				{
					obj_meta->rect_params.has_bg_color = 0;
				}
			}

			if(!app_ctx->show_bbox_text)
				continue;

			obj_meta->text_params.x_offset = obj_meta->rect_params.left;
			obj_meta->text_params.y_offset = obj_meta->rect_params.top - 30;
			obj_meta->text_params.font_params.font_color = app_ctx->config.osd_config.text_color;
			obj_meta->text_params.font_params.font_size = app_ctx->config.osd_config.text_size;
			obj_meta->text_params.font_params.font_name = g_strdup(app_ctx->config.osd_config.font.c_str());
			if(app_ctx->config.osd_config.text_has_bg)
			{
				obj_meta->text_params.set_bg_clr = 1;
				obj_meta->text_params.text_bg_clr = app_ctx->config.osd_config.text_bg_color;
			}

			obj_meta->text_params.display_text = (char *)g_malloc(128);
			obj_meta->text_params.display_text[0] = '\0';
			str_ins_pos = obj_meta->text_params.display_text;

			if(obj_meta->obj_label[0] != '\0')
				sprintf(str_ins_pos, "%s", obj_meta->obj_label);
			str_ins_pos += strlen(str_ins_pos);

			if(obj_meta->object_id != UNTRACKED_OBJECT_ID)
			{
				/** id is a 64-bit sequential value;
				 * but considering the display aesthetic,
				 * trimming to lower 32-bits */
				if(app_ctx->config.tracker_config.display_tracking_id)
				{
					uint64_t const LOW_32_MASK = 0x00000000FFFFFFFF;
					sprintf(str_ins_pos, " %lu", (obj_meta->object_id & LOW_32_MASK));
					str_ins_pos += strlen(str_ins_pos);
				}
			}

			obj_meta->classifier_meta_list = g_list_sort(obj_meta->classifier_meta_list, component_id_compare_func);
			for(NvDsMetaList *l_class = obj_meta->classifier_meta_list; l_class != nullptr; l_class = l_class->next)
			{
				auto *cmeta = reinterpret_cast<NvDsClassifierMeta *>(l_class->data);
				for(NvDsMetaList *l_label = cmeta->label_info_list; l_label != nullptr; l_label = l_label->next)
				{
					auto *label = reinterpret_cast<NvDsLabelInfo *>(l_label->data);
					if(label->pResult_label)
					{
						sprintf(str_ins_pos, " %s", label->pResult_label);
					}
					else if(label->result_label[0] != '\0')
					{
						sprintf(str_ins_pos, " %s", label->result_label);
					}
					str_ins_pos += strlen(str_ins_pos);
				}
			}
		}
	}
}

/**
 * Function which processes the inferred buffer and its metadata.
 * It also gives opportunity to attach application specific
 * metadata (e.g. clock, analytics output etc.).
 */
static void process_buffer(GstBuffer *buffer, AppContext *app_ctx, uint index)
{
	NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
	if(!batch_meta)
	{
		TADS_WARN_MSG_V("Batch meta not found for buffer %p", buffer);
		return;
	}
	// TODO DEBUG:PROCESS_META
	// process_meta(app_ctx, batch_meta);
	InstanceData *data = &app_ctx->instance_data[index];
	data->frame_num++;

	/* Opportunity to modify the processed metadata or do analytics based on
	 * type of object e.g. maintaining count of particular type of car.
	 */
	// app_ctx->all_bbox_generated(buffer, batch_meta, index);

	/*
	 * callback to attach application specific additional metadata.
	 */
	app_ctx->overlay_graphics(buffer, batch_meta, index);
}

/**
 * Probe function to get results after all inferences(Primary + Secondary)
 * are done. This will be just before OSD or sink (in case OSD is disabled).
 */
static GstPadProbeReturn gie_processing_done_buf_prob([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *buffer = reinterpret_cast<GstBuffer *>(info->data);
	auto *instance_bin = reinterpret_cast<InstanceBin *>(data);
	uint index = instance_bin->index;
	AppContext *app_ctx = instance_bin->app_ctx;

	if(gst_buffer_is_writable(buffer))
		process_buffer(buffer, app_ctx, index);
	return GST_PAD_PROBE_OK;
}

/**
 * Buffer probe function after tracker.
 */
static GstPadProbeReturn analytics_done_buf_prob(GstPad *, GstPadProbeInfo *info, void *data)
{
	auto common_elements = reinterpret_cast<InstanceBin *>(data);
	AppContext *app_ctx = common_elements->app_ctx;
	auto *buffer = reinterpret_cast<GstBuffer *>(info->data);
	NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
	if(!batch_meta)
	{
		TADS_WARN_MSG_V("Batch meta not found for buffer %p", buffer);
		return GST_PAD_PROBE_OK;
	}
	app_ctx->all_bbox_generated(buffer, batch_meta);

	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn latency_measurement_buf_prob(GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *app_ctx = reinterpret_cast<AppContext *>(data);
	uint i, num_sources_in_batch;
	if(nvds_enable_latency_measurement)
	{
		auto *buffer = reinterpret_cast<GstBuffer *>(info->data);
		NvDsFrameLatencyInfo *latency_info;
		g_mutex_lock(&app_ctx->latency_lock);
		latency_info = app_ctx->latency_info_array;
		uint64_t batch_num = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(pad), "latency-batch-num"));
		g_print("\n*********** BATCH-NUM = %lu *************\n", batch_num);

		num_sources_in_batch = nvds_measure_buffer_latency(buffer, latency_info);

		for(i = 0; i < num_sources_in_batch; i++)
		{
			g_print("Source id = %d Frame_num = %d Frame latency = %lf (ms) \n", latency_info[i].source_id,
							latency_info[i].frame_num, latency_info[i].latency);
		}
		g_mutex_unlock(&app_ctx->latency_lock);
		g_object_set_data(G_OBJECT(pad), "latency-batch-num", GSIZE_TO_POINTER(batch_num + 1));
	}

	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
demux_latency_measurement_buf_prob([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *app_ctx = reinterpret_cast<AppContext *>(data);
	uint i, num_sources_in_batch;
	if(nvds_enable_latency_measurement)
	{
		auto *buffer = reinterpret_cast<GstBuffer *>(info->data);
		NvDsFrameLatencyInfo *latency_info;
		g_mutex_lock(&app_ctx->latency_lock);
		latency_info = app_ctx->latency_info_array;
		g_print("\n************DEMUX BATCH-NUM = %d**************\n", demux_batch_num);
		num_sources_in_batch = nvds_measure_buffer_latency(buffer, latency_info);

		for(i = 0; i < num_sources_in_batch; i++)
		{
			g_print("Source id = %d Frame_num = %d Frame latency = %lf (ms) \n", latency_info[i].source_id,
							latency_info[i].frame_num, latency_info[i].latency);
		}
		g_mutex_unlock(&app_ctx->latency_lock);
		demux_batch_num++;
	}

	return GST_PAD_PROBE_OK;
}

static bool add_and_link_broker_sink(AppContext *app_ctx)
{
	AppConfig *config{ &app_ctx->config };
	/** Only first instance_bin broker sink
	 * employed as there's only one analytics path for N sources
	 * NOTE: There shall be only one [sink] group
	 * with type=6 (NV_DS_SINK_MSG_CONV_BROKER)
	 * a) Multiple of them does not make sense as we have only
	 * one analytics pipe generating the data for broker sink
	 * b) If Multiple broker sinks are configured by the user
	 * in config file, only the first in the order of
	 * appearance will be considered
	 * and others shall be ignored
	 * c) Ideally it should be documented (or obvious) that:
	 * multiple [sink] groups with type=6 (NV_DS_SINK_MSG_CONV_BROKER)
	 * is invalid
	 */
	InstanceBin *instance_bin{ &app_ctx->pipeline.instance_bins.at(0) };
	Pipeline *pipeline{ &app_ctx->pipeline };
	SinkSubBin *sink_sub_bin;

	for(uint i = 0; i < config->num_sink_sub_bins; i++)
	{
		if(config->sink_bin_sub_bin_configs[i].type == SinkType::MSG_CONV_BROKER)
		{
			sink_sub_bin = &instance_bin->sink.sub_bins.at(i);
			if(!pipeline->common_elements.tee)
			{
				TADS_ERR_MSG_V("%s failed; broker added without analytics; check config file\n", __func__);
				return false;
			}
			/** add the broker sink bin to pipeline */
			if(!gst_bin_add(GST_BIN(pipeline->pipeline), sink_sub_bin->bin))
			{
				return false;
			}
			/** link the broker sink bin to the common_elements tee
			 * (The tee after nvinfer -> tracker (optional) -> sgies (optional) block) */
			if(!gst::link_element_to_tee_src_pad(pipeline->common_elements.tee, sink_sub_bin->bin))
			{
				return false;
			}
		}
	}
	return true;
}

static bool is_sink_available_for_source_id(AppConfig *config, uint source_id)
{
	SinkSubBinConfig *sub_bin_config;
	for(uint j = 0; j < config->num_sink_sub_bins; j++)
	{
		sub_bin_config = &config->sink_bin_sub_bin_configs.at(j);
		if(sub_bin_config->enable && sub_bin_config->source_id == source_id && !sub_bin_config->link_to_demux)
		{
			return true;
		}
	}
	return false;
}

bool AppContext::create_pipeline(perf_callback perf_cb)
{
#ifdef TADS_APP_DEBUG
	TADS_DBG_MSG_V("Creating app pipeline");
#endif
	bool success{};
	SourceParentBin *multi_src_bin{ &pipeline.multi_src_bin };
	std::string elem_name;
	GstBus *bus;
	GstElement *last_elem;
	GstElement *tmp_elem1;
	GstElement *tmp_elem2;
	uint i;
	GstPad *fps_pad{};
	gulong latency_probe_id;
	InstanceBin *instance_bin;

	g_dsmeta_quark = g_quark_from_static_string(NVDS_META_STRING);
	this->sensor_info_hash = g_hash_table_new(nullptr, nullptr);
	this->perf_struct.fps_info_hash = g_hash_table_new_full(g_direct_hash, g_direct_equal, nullptr, nullptr);

	if(config.osd_config.num_out_buffers < 8)
	{
		config.osd_config.num_out_buffers = 8;
	}

	elem_name = "app-pipeline";
	pipeline.pipeline = gst_pipeline_new(elem_name.c_str());
	if(!pipeline.pipeline)
	{
		TADS_ERR_MSG_V("Failed to create %s", elem_name.c_str());
		goto done;
	}
#ifdef TADS_APP_DEBUG
	TADS_DBG_MSG_V("Created pipeline %s", elem_name.c_str());
#endif

	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline.pipeline));
	pipeline.bus_id = gst_bus_add_watch(bus, reinterpret_cast<GstBusFunc>(bus_callback), this);
	gst_object_unref(bus);

	if(config.file_loop)
	{
#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("File loop enabled");
#endif
		/* Let each source bin know it needs to loop. */
		for(uint j = 0; j < config.num_source_sub_bins; j++)
		{
			config.multi_source_configs.at(j).loop = true;
		}
	}

	// Add muxer and < N > source components to the pipeline based
	// on the settings in configuration file.
	if(config.use_nvmultiurisrcbin)
	{
		if(config.num_source_sub_bins > 0)
		{
			if(!create_nvmultiurisrcbin_bin(config.num_source_sub_bins, config.multi_source_configs, multi_src_bin))
				goto done;
		}
		else
		{
			if(!config.source_attr_all_parsed)
			{
				TADS_ERR_MSG_V("[source-attr-all] config group not set, needs to be configured");
				goto done;
			}
			if(!create_nvmultiurisrcbin_bin(config.num_source_sub_bins, { config.source_attr_all_config }, multi_src_bin))
				goto done;
			// [source-list] added with num-source-bins=0; This means source-bin
			// will be created and be waiting for source adds over REST API
			// mark num-source-bins=1 as one source-bin is indeed created
			config.num_source_sub_bins = 1;
		}
		/** set properties for nvmultiurisrcbin */
		if(!config.uri_list.empty())
		{
			std::string uri_list_comma_sep{ join(config.uri_list, ",") };
			g_object_set(multi_src_bin->nvmultiurisrcbin, "uri-list", uri_list_comma_sep.c_str(), nullptr);
		}
		if(!config.sensor_id_list.empty())
		{
			std::string uri_list_comma_sep{ join(config.sensor_id_list, ",") };
			g_object_set(multi_src_bin->nvmultiurisrcbin, "sensor-id-list", uri_list_comma_sep.c_str(), nullptr);
		}
		if(!config.sensor_name_list.empty())
		{
			std::string uri_list_comma_sep{ join(config.sensor_name_list, ",") };
			g_object_set(multi_src_bin->nvmultiurisrcbin, "sensor-name-list", uri_list_comma_sep.c_str(), nullptr);
		}
		g_object_set(multi_src_bin->nvmultiurisrcbin, "max-batch-size", config.max_batch_size, nullptr);
		g_object_set(multi_src_bin->nvmultiurisrcbin, "ip-address", config.http_ip.c_str(), nullptr);
		g_object_set(multi_src_bin->nvmultiurisrcbin, "port", config.http_port.c_str(), nullptr);
	}
	else
	{
		if(!create_multi_source_bin(config.num_source_sub_bins, config.multi_source_configs, &pipeline.multi_src_bin))
			goto done;
	}
	gst_bin_add(GST_BIN(pipeline.pipeline), multi_src_bin->bin);

	if(config.streammux_config.is_parsed)
	{
		if(config.use_nvmultiurisrcbin)
		{
			config.streammux_config.use_nvmultiurisrcbin = true;
			// overriding mux_config.batch_size to max_batch_size
			config.streammux_config.batch_size = config.max_batch_size;
		}

		if(!set_streammux_properties(&config.streammux_config, multi_src_bin->streammux))
		{
			TADS_WARN_MSG_V("Failed to set streammux properties");
		}
	}

	if(this->latency_info_array == nullptr)
	{
		this->latency_info_array = new NvDsFrameLatencyInfo[config.streammux_config.batch_size];
	}

	// a tee after the tiler which shall be connected to sink(s)
	elem_name = "tiler_tee";
	pipeline.tiler_tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!pipeline.tiler_tee)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}
#ifdef TADS_APP_DEBUG
	TADS_DBG_MSG_V("Created pipeline %s", elem_name.c_str());
#endif

	gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.tiler_tee);

	// Tiler + Demux in Parallel Use-Case
	if(config.tiled_display_config.enable == TiledDisplayState::ENABLED_WITH_PARALLEL_DEMUX)
	{
		elem_name = "demuxer";
		pipeline.demuxer = gst::element_factory_make(TADS_ELEM_STREAM_DEMUX, elem_name);
		if(!pipeline.demuxer)
		{
			TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.demuxer);

		/** NOTE:
		 * demux output is supported for only one source
		 * If multiple [sink] groups are configured with
		 * link_to_demux=1, only the first [sink]
		 * shall be constructed for all occurences of
		 * [sink] groups with link_to_demux=1
		 */
		{
			std::string pad_name;
			GstPad *demux_src_pad;
			InstanceBin *demux_instance_bin;

			i = 0;
			if(!this->create_demux_pipeline(i))
			{
				goto done;
			}

			for(; i < config.num_sink_sub_bins; i++)
			{
				if(config.sink_bin_sub_bin_configs[i].link_to_demux)
				{
					pad_name = fmt::format("src_{:02}", config.sink_bin_sub_bin_configs.at(i).source_id);
					break;
				}
			}

			if(i >= config.num_sink_sub_bins)
			{
				TADS_ERR_MSG_V("sink for demux (use link-to-demux-only property) is not provided in the config file");
				goto done;
			}

			i = 0;

			demux_instance_bin = &pipeline.demux_instance_bins.at(i);
			gst_bin_add(GST_BIN(pipeline.pipeline), demux_instance_bin->bin);

			demux_src_pad = gst::element_request_pad_simple(pipeline.demuxer, pad_name);
			TADS_LINK_ELEMENT_FULL(pipeline.demuxer, pad_name.c_str(), demux_instance_bin->bin, "sink");
			gst_object_unref(demux_src_pad);

			TADS_ELEM_ADD_PROBE(latency_probe_id, this->pipeline.demux_instance_bins.at(i).demux_sink.bin, "sink",
													demux_latency_measurement_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, this);
			latency_probe_id = latency_probe_id;
		}

		last_elem = pipeline.demuxer;
		gst::link_element_to_tee_src_pad(pipeline.tiler_tee, last_elem);
		//		last_elem = pipeline->tiler_tee;
	}
	if(config.tiled_display_config.enable == TiledDisplayState::ENABLED)
	{
		/* Tiler will generate a single composited buffer for all sources. So need
		 * to create only one processing instance. */
		if(!create_processing_instance())
		{
			goto done;
		}
		instance_bin = &pipeline.instance_bins.at(0);
		TiledDisplayConfig *tiled_display_config{ &config.tiled_display_config };

		// create and add tiling component to pipeline.
		if(tiled_display_config->columns * tiled_display_config->rows < config.num_source_sub_bins)
		{
			if(tiled_display_config->columns == 0)
			{
				tiled_display_config->columns = static_cast<uint>(std::lround(sqrt(config.num_source_sub_bins)));
			}
			tiled_display_config->rows = (uint)std::ceil(1.0 * config.num_source_sub_bins / tiled_display_config->columns);
			TADS_WARN_MSG_V("Num of Tiles less than number of sources, readjusting to "
											"%u rows, %u columns",
											tiled_display_config->rows, tiled_display_config->columns);
		}

		gst_bin_add(GST_BIN(pipeline.pipeline), instance_bin->bin);
		last_elem = instance_bin->bin;

		if(!create_tiled_display_bin(tiled_display_config, &pipeline.tiled_display))
		{
			goto done;
		}
		gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.tiled_display.bin);
		TADS_LINK_ELEMENT(pipeline.tiled_display.bin, last_elem);
		last_elem = pipeline.tiled_display.bin;

		gst::link_element_to_tee_src_pad(pipeline.tiler_tee, last_elem);
		last_elem = pipeline.tiler_tee;

		TADS_ELEM_ADD_PROBE(latency_probe_id, instance_bin->sink.sub_bins.at(0).sink, "sink", latency_measurement_buf_prob,
												GST_PAD_PROBE_TYPE_BUFFER, this);
	}
	else
	{
		// create demuxer only if tiled display is disabled.
		elem_name = "demuxer";
		pipeline.demuxer = gst::element_factory_make(TADS_ELEM_STREAM_DEMUX, elem_name);
		if(!pipeline.demuxer)
		{
			TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.demuxer);

		for(i = 0; i < config.num_source_sub_bins; i++)
		{
			char pad_name[16];
			GstPad *demux_src_pad;
			instance_bin = &pipeline.instance_bins.at(i);

			// Check if any sink has been configured to render/encode output for
			// source instance_num `i`. The processing instance for that source will be
			// created only if atleast one sink has been configured as such.
			if(!is_sink_available_for_source_id(&config, i))
				continue;

			if(!create_processing_instance(i))
			{
				goto done;
			}
			gst_bin_add(GST_BIN(pipeline.pipeline), instance_bin->bin);

			g_snprintf(pad_name, 16, "src_%02d", i);
			demux_src_pad = gst::element_request_pad_simple(pipeline.demuxer, pad_name);
			TADS_LINK_ELEMENT_FULL(pipeline.demuxer, pad_name, instance_bin->bin, "sink");
			gst_object_unref(demux_src_pad);

			for(auto &sub_bin : instance_bin->sink.sub_bins)
			{
				if(sub_bin.sink)
				{
					TADS_ELEM_ADD_PROBE(latency_probe_id, sub_bin.sink, "sink",
															reinterpret_cast<GstPadProbeCallback>(latency_measurement_buf_prob),
															GST_PAD_PROBE_TYPE_BUFFER, this);
					break;
				}
			}

			latency_probe_id = latency_probe_id;
		}
		last_elem = pipeline.demuxer;
	}

	if(config.tiled_display_config.enable == TiledDisplayState::DISABLED)
	{
		fps_pad = gst_element_get_static_pad(pipeline.demuxer, "sink");
	}
	else
	{
		fps_pad = gst_element_get_static_pad(pipeline.tiled_display.bin, "sink");
	}

	pipeline.common_elements.app_ctx = this;
	// create and add common components to pipeline.
	if(!this->create_common_elements(&tmp_elem1, &tmp_elem2))
	{
		goto done;
	}

	if(!add_and_link_broker_sink(this))
	{
		goto done;
	}

	if(tmp_elem2)
	{
		TADS_LINK_ELEMENT(tmp_elem2, last_elem);
		last_elem = tmp_elem1;
	}

	TADS_LINK_ELEMENT(pipeline.multi_src_bin.bin, last_elem);

	// enable performance measurement and add call back function to receive
	// performance data.
	if(config.enable_perf_measurement)
	{
		this->perf_struct.context = this;
		if(config.use_nvmultiurisrcbin)
		{
			this->perf_struct.stream_name_display = config.stream_name_display;
			this->perf_struct.use_nvmultiurisrcbin = config.use_nvmultiurisrcbin;
			enable_perf_measurement(&this->perf_struct, fps_pad, config.max_batch_size, config.perf_measurement_interval_sec,
															perf_cb);
		}
		else
		{
			enable_perf_measurement(&this->perf_struct, fps_pad, pipeline.multi_src_bin.num_bins,
															config.perf_measurement_interval_sec, perf_cb);
		}
	}

	if(config.num_message_consumers)
	{
		for(i = 0; i < config.num_message_consumers; i++)
		{
			this->c2d_contexts.at(i) = start_cloud_to_device_messaging(&config.message_consumer_configs[i], subscribe_cb,
																																 &this->pipeline.multi_src_bin);
			if(!this->c2d_contexts.at(i))
			{
				TADS_ERR_MSG_V("Failed to create message consumer");
				goto done;
			}
		}
	}

	GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(this->pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "ds-app-null");

	g_mutex_init(&this->app_lock);
	g_cond_init(&this->app_cond);
	g_mutex_init(&this->latency_lock);

	success = true;
#ifdef TADS_APP_DEBUG
	TADS_DBG_MSG_V("Pipeline created successfuly");
#endif

done:
	if(fps_pad)
	{
		gst_object_unref(fps_pad);
	}

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return success;
}

bool AppContext::pause_pipeline()
{
	GstState cur;
	GstState pending;
	GstStateChangeReturn ret;
	GstClockTime timeout = 5 * GST_SECOND / 1000;
	GTimer *timer;

	ret = gst_element_get_state(this->pipeline.pipeline, &cur, &pending, timeout);

	if(ret == GST_STATE_CHANGE_ASYNC)
	{
		return false;
	}

	if(cur == GST_STATE_PAUSED)
	{
		return true;
	}
	else if(cur == GST_STATE_PLAYING)
	{
		gst_element_set_state(this->pipeline.pipeline, GST_STATE_PAUSED);
		gst_element_get_state(this->pipeline.pipeline, &cur, &pending, GST_CLOCK_TIME_NONE);
		pause_perf_measurement(&this->perf_struct);
		timer = this->pipeline.common_elements.analytics.timer;
		if(timer)
			g_timer_stop(timer);

		return true;
	}
	else
	{
		return false;
	}
}

bool AppContext::resume_pipeline()
{
	GstState cur;
	GstState pending;
	GstStateChangeReturn ret;
	GstClockTime timeout = 5 * GST_SECOND / 1000;
	GTimer *timer;

	ret = gst_element_get_state(this->pipeline.pipeline, &cur, &pending, timeout);

	if(ret == GST_STATE_CHANGE_ASYNC)
	{
		return false;
	}

	if(cur == GST_STATE_PLAYING)
	{
		return true;
	}
	else if(cur == GST_STATE_PAUSED)
	{
		gst_element_set_state(this->pipeline.pipeline, GST_STATE_PLAYING);
		gst_element_get_state(this->pipeline.pipeline, &cur, &pending, GST_CLOCK_TIME_NONE);
		resume_perf_measurement(&this->perf_struct);
		timer = this->pipeline.common_elements.analytics.timer;
		if(timer)
			g_timer_continue(timer);
		return true;
	}
	else
	{
		return false;
	}
}

void AppContext::destroy_pipeline()
{
	gint64 end_time;
	uint i;
	GstBus *bus;

	end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

	if(this->pipeline.demuxer)
	{
		GstPad *gstpad = gst_element_get_static_pad(this->pipeline.demuxer, "sink");
		gst_pad_send_event(gstpad, gst_event_new_eos());
		gst_object_unref(gstpad);
	}
	else if(this->pipeline.multi_src_bin.streammux)
	{
		std::string pad_name;
		for(i = 0; i < config.num_source_sub_bins; i++)
		{
			GstPad *gstpad;
			pad_name = fmt::format("sink_{}", i);
			gstpad = gst_element_get_static_pad(this->pipeline.multi_src_bin.streammux, pad_name.c_str());
			if(gstpad)
			{
				/** When using nvmultiurisrcbin, gstpad will be nullptr
				 * EOS for the pad on pipeline teardown
				 * is auto handled within nvmultiurisrcbin */
				gst_pad_send_event(gstpad, gst_event_new_eos());
				gst_object_unref(gstpad);
			}
		}
	}
	else if(this->pipeline.instance_bins[0].sink.bin)
	{
		GstPad *gstpad = gst_element_get_static_pad(this->pipeline.instance_bins[0].sink.bin, "sink");
		gst_pad_send_event(gstpad, gst_event_new_eos());
		gst_object_unref(gstpad);
	}

	g_usleep(100000);

	g_mutex_lock(&this->app_lock);
	if(this->pipeline.pipeline)
	{
		destroy_smart_record_bin(&this->pipeline.multi_src_bin);
		bus = gst_pipeline_get_bus(GST_PIPELINE(this->pipeline.pipeline));

		while(true)
		{
			GstMessage *message = gst_bus_pop(bus);
			if(message == nullptr)
				break;
			else if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
				bus_callback(bus, message, this);
			else
				gst_message_unref(message);
		}
		gst_object_unref(bus);
		gst_element_set_state(this->pipeline.pipeline, GST_STATE_NULL);
		if(this->pipeline.common_elements.obj_enc_ctx_handle)
			nvds_obj_enc_destroy_context(this->pipeline.common_elements.obj_enc_ctx_handle);
	}
	g_cond_wait_until(&this->app_cond, &this->app_lock, end_time);
	g_mutex_unlock(&this->app_lock);

	for(i = 0; i < this->config.num_source_sub_bins; i++)
	{
		InstanceBin *instance_bin{ &this->pipeline.instance_bins.at(i) };
		if(config.osd_config.enable)
		{
			TADS_ELEM_REMOVE_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->osd.nvosd, "sink");
		}
		else
		{
			TADS_ELEM_REMOVE_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->sink.bin, "sink");
		}

		if(config.primary_gie_config.enable)
		{
			TADS_ELEM_REMOVE_PROBE(instance_bin->primary_bbox_buffer_probe_id, instance_bin->primary_gie.bin, "src");
		}
	}
	if(this->latency_info_array == nullptr)
	{
		delete[] this->latency_info_array;
		this->latency_info_array = nullptr;
	}

	if(this->config.analytics_config.enable)
	{
		AnalyticsBin *analytics_bin{ &this->pipeline.common_elements.analytics };
		g_timer_stop(analytics_bin->timer);
		g_timer_destroy(analytics_bin->timer);
		g_date_time_unref(analytics_bin->date_time);
	}

	destroy_sink_bin();
	g_mutex_clear(&this->latency_lock);

	if(this->pipeline.pipeline)
	{
		bus = gst_pipeline_get_bus(GST_PIPELINE(this->pipeline.pipeline));
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);
		gst_object_unref(this->pipeline.pipeline);
		this->pipeline.pipeline = nullptr;
		pause_perf_measurement(&this->perf_struct);

		// for pipeline-recreate, reset rtsp srouce's depay, such as rtph264depay.
		SourceParentBin *pbin = &this->pipeline.multi_src_bin;
		if(pbin)
		{
			for(auto &src_bin : pbin->sub_bins)
			{
				if(src_bin.config && src_bin.config->type == SourceType::RTSP)
				{
					src_bin.depay = nullptr;
				}
			}
		}
	}

	if(config.num_message_consumers)
	{
		for(i = 0; i < config.num_message_consumers; i++)
		{
			if(this->c2d_contexts.at(i))
				stop_cloud_to_device_messaging(this->c2d_contexts.at(i));
		}
	}
}

void AppContext::all_bbox_generated(GstBuffer *buffer, NvDsBatchMeta *batch_meta)
{
	if(config.analytics_config.enable)
	{
		parse_analytics_metadata(this, buffer, batch_meta);
	}
}

bool AppContext::overlay_graphics(GstBuffer *, NvDsBatchMeta *batch_meta, uint index)
{
	int src_index = this->active_source_index;
	if(src_index == -1)
		return true;

	NvDsFrameLatencyInfo *latency_info;
	NvDsDisplayMeta *display_meta = nvds_acquire_display_meta_from_pool(batch_meta);

	display_meta->num_labels = 1;
	display_meta->text_params[0].display_text =
			g_strdup_printf("Source: %s", this->config.multi_source_configs[src_index].uri.c_str());

	display_meta->text_params[0].y_offset = 20;
	display_meta->text_params[0].x_offset = 20;
	display_meta->text_params[0].font_params.font_color = { 0, 1, 0, 1 };
	display_meta->text_params[0].font_params.font_size = this->config.osd_config.text_size * 1.5;
	display_meta->text_params[0].font_params.font_name = "Serif";
	display_meta->text_params[0].set_bg_clr = 1;
	display_meta->text_params[0].text_bg_clr = { 0, 0, 0, 1.0 };

	if(nvds_enable_latency_measurement)
	{
		g_mutex_lock(&this->latency_lock);
		latency_info = &this->latency_info_array[index];
		display_meta->num_labels++;
		display_meta->text_params[1].display_text = g_strdup_printf("Latency: %lf", latency_info->latency);
		g_mutex_unlock(&this->latency_lock);

		display_meta->text_params[1].y_offset =
				(display_meta->text_params[0].y_offset * 2) + display_meta->text_params[0].font_params.font_size;
		display_meta->text_params[1].x_offset = 20;
		display_meta->text_params[1].font_params.font_color = { 0, 1, 0, 1 };
		display_meta->text_params[1].font_params.font_size = this->config.osd_config.text_size * 1.5;
		display_meta->text_params[1].font_params.font_name = "Arial";
		display_meta->text_params[1].set_bg_clr = 1;
		display_meta->text_params[1].text_bg_clr = { 0, 0, 0, 1.0 };
	}

	nvds_add_display_meta_to_frame(nvds_get_nth_frame_meta(batch_meta->frame_meta_list, 0), display_meta);
	return true;
}

bool AppContext::create_common_elements(GstElement **sink_elem, GstElement **src_elem)
{
#ifdef TADS_APP_DEBUG
	TADS_DBG_MSG_V("Creating common elements");
#endif

	bool success{};
	std::string elem_name;
	*sink_elem = *src_elem = nullptr;

	if(config.primary_gie_config.enable)
	{
		if(config.num_secondary_gie_sub_bins > 0)
		{
			SecondaryGieBin *secondary_gie{ &pipeline.common_elements.secondary_gie };
			// if using nvmultiurisrcbin, override batch-size config for sgie
			if(config.use_nvmultiurisrcbin)
			{
				for(uint i = 0; i < config.num_secondary_gie_sub_bins; i++)
				{
					config.secondary_gie_sub_bin_configs.at(i).batch_size = config.sgie_batch_size;
				}
			}
			if(!create_secondary_gie(config.num_secondary_gie_sub_bins, config.primary_gie_config.unique_id,
															 config.secondary_gie_sub_bin_configs, secondary_gie))
			{
				goto done;
			}

#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Adding secondary_gie bin to pipeline");
#endif
			gst_bin_add(GST_BIN(pipeline.pipeline), secondary_gie->bin);
			if(!*src_elem)
			{
				*src_elem = secondary_gie->bin;
#ifdef TADS_APP_DEBUG
				TADS_DBG_MSG_V("Current source element pointer set to secondary_gie bin");
#endif
			}
			if(*sink_elem)
			{
#ifdef TADS_APP_DEBUG
				TADS_DBG_MSG_V("Linking secondary_gie bin element to sink_elem");
#endif
				TADS_LINK_ELEMENT(secondary_gie->bin, *sink_elem);
			}
			*sink_elem = secondary_gie->bin;
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Current sink element pointer set to secondary_gie bin");
#endif
		}
	}

	if(config.primary_gie_config.enable)
	{
		if(config.num_secondary_preprocess_sub_bins > 0)
		{
			SecondaryPreProcessBin *secondary_pre_process{ &pipeline.common_elements.secondary_pre_process };

			if(!create_secondary_preprocess_bin(config.num_secondary_preprocess_sub_bins, config.primary_gie_config.unique_id,
																					config.secondary_preprocess_sub_bin_configs, secondary_pre_process))
			{
				TADS_ERR_MSG_V("creating secondary_preprocess bin failed");
				goto done;
			}

#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Adding secondary_pre_process bin to pipeline");
#endif
			gst_bin_add(GST_BIN(pipeline.pipeline), secondary_pre_process->bin);

			if(!*src_elem)
			{
				*src_elem = secondary_pre_process->bin;
#ifdef TADS_APP_DEBUG
				TADS_DBG_MSG_V("Current source element pointer set to secondary_pre_process bin");
#endif
			}
			if(*sink_elem)
			{
#ifdef TADS_APP_DEBUG
				TADS_DBG_MSG_V("Linking secondary_pre_process bin element to sink_elem");
#endif
				TADS_LINK_ELEMENT(secondary_pre_process->bin, *sink_elem);
			}

			*sink_elem = secondary_pre_process->bin;
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Current sink element pointer set to secondary_pre_process bin");
#endif
		}
	}

	if(config.analytics_config.enable)
	{
		AnalyticsBin *analytics;

		if(!create_analytics_bin(&config.analytics_config, &pipeline.common_elements.analytics))
		{
			TADS_ERR_MSG_V("creating dsanalytics bin failed");
			goto done;
		}
		analytics = &pipeline.common_elements.analytics;

#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Adding analytics bin to pipeline");
#endif
		gst_bin_add(GST_BIN(pipeline.pipeline), analytics->bin);

		if(!*src_elem)
		{
			*src_elem = analytics->bin;
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Current source element pointer set to analytics bin");
#endif
		}
		if(*sink_elem)
		{
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Linking analytics bin element to sink_elem");
#endif
			TADS_LINK_ELEMENT(analytics->bin, *sink_elem);
		}
		*sink_elem = analytics->bin;
#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Current sink element pointer set to analytics bin");
#endif
	}

	if(config.tracker_config.enable)
	{
		TrackerBin *tracker;
		if(!create_tracking_bin(&config.tracker_config, &pipeline.common_elements.tracker))
		{
			TADS_ERR_MSG_V("creating tracker bin failed\n");
			goto done;
		}
		tracker = &pipeline.common_elements.tracker;

#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Adding tracker bin to pipeline");
#endif
		gst_bin_add(GST_BIN(pipeline.pipeline), tracker->bin);
		if(!*src_elem)
		{
			*src_elem = tracker->bin;
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Current source element pointer set to tracker bin");
#endif
		}
		if(*sink_elem)
		{
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Linking tracker bin element to sink_elem");
#endif
			TADS_LINK_ELEMENT(tracker->bin, *sink_elem);
		}
		*sink_elem = tracker->bin;
#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Current sink element pointer set to tracker bin");
#endif
	}

	if(config.primary_gie_config.enable)
	{
		PrimaryGieBin *primary_gie{ &pipeline.common_elements.primary_gie };
		// if using nvmultiurisrcbin, override batch-size config for pgie
		if(config.use_nvmultiurisrcbin)
		{
			config.primary_gie_config.batch_size = config.max_batch_size;
		}

		if(!create_primary_gie(&config.primary_gie_config, primary_gie))
		{
			goto done;
		}

#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Adding primary_gie bin to pipeline");
#endif
		gst_bin_add(GST_BIN(pipeline.pipeline), primary_gie->bin);
		if(*sink_elem)
		{
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Linking primary_gie bin element to sink_elem");
#endif
			TADS_LINK_ELEMENT(primary_gie->bin, *sink_elem);
		}
		*sink_elem = primary_gie->bin;
		if(!*src_elem)
		{
			*src_elem = primary_gie->bin;
#ifdef TADS_APP_DEBUG
			TADS_DBG_MSG_V("Current source element pointer set to primary_gie bin");
#endif
		}
	}

	if(config.preprocess_config.enable)
	{
		PreProcessBin *pre_process{ &pipeline.common_elements.preprocess };
		if(!create_preprocess_bin(&config.preprocess_config, pre_process))
		{
			TADS_ERR_MSG_V("Creating preprocess bin failed");
			goto done;
		}
		gst_bin_add(GST_BIN(pipeline.pipeline), pre_process->bin);

		if(!*src_elem)
		{
			*src_elem = pre_process->bin;
		}
		if(*sink_elem)
		{
			TADS_LINK_ELEMENT(pre_process->bin, *sink_elem);
		}

		*sink_elem = pre_process->bin;
	}

	if(config.image_save_config.enable)
	{
		pipeline.common_elements.obj_enc_ctx_handle = nvds_obj_enc_create_context(config.image_save_config.gpu_id);

		if(!pipeline.common_elements.obj_enc_ctx_handle)
		{
			TADS_ERR_MSG_V("Unable to create context");
			goto done;
		}
	}

	if(*src_elem)
	{
		TADS_ELEM_ADD_PROBE(pipeline.common_elements.primary_bbox_buffer_probe_id, *src_elem, "src",
												reinterpret_cast<GstPadProbeCallback>(analytics_done_buf_prob), GST_PAD_PROBE_TYPE_BUFFER,
												&pipeline.common_elements);

		// Add common message converter
		if(config.msg_conv_config.enable)
		{
			SinkMsgConvBrokerConfig *conv_config = &config.msg_conv_config;
			elem_name = "common_msg_conv";
			pipeline.common_elements.msg_conv = gst::element_factory_make(TADS_ELEM_MSG_CONV, elem_name);
			if(!pipeline.common_elements.msg_conv)
			{
				TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
				goto done;
			}

			g_object_set(G_OBJECT(pipeline.common_elements.msg_conv), "config", conv_config->config_file_path.c_str(),
									 "msg2p-lib", (!conv_config->conv_msg2p_lib.empty() ? conv_config->conv_msg2p_lib.c_str() : "null"),
									 "payload-type", conv_config->conv_payload_type, "comp-id", conv_config->conv_comp_id,
									 "debug-payload-dir", conv_config->debug_payload_dir.c_str(), "multiple-payloads",
									 conv_config->multiple_payloads, "msg2p-newapi", conv_config->conv_msg2p_new_api, nullptr);

			gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.common_elements.msg_conv);

			TADS_LINK_ELEMENT(*src_elem, pipeline.common_elements.msg_conv);
			*src_elem = pipeline.common_elements.msg_conv;
		}

		elem_name = "common_analytics_tee";
		pipeline.common_elements.tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
		if(!pipeline.common_elements.tee)
		{
			TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
			goto done;
		}

		gst_bin_add(GST_BIN(pipeline.pipeline), pipeline.common_elements.tee);

#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Linking src_elem element with pipeline tee element");
#endif
		TADS_LINK_ELEMENT(*src_elem, pipeline.common_elements.tee);
		*src_elem = pipeline.common_elements.tee;
#ifdef TADS_APP_DEBUG
		TADS_DBG_MSG_V("Current source element pointer set to pipeline tee element");
#endif
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool AppContext::create_processing_instance(uint index)
{
	bool success{};
	InstanceBin *instance_bin{ &this->pipeline.instance_bins.at(index) };
	GstElement *last_elem;
	std::string elem_name;

	instance_bin->index = index;
	instance_bin->app_ctx = this;

	elem_name = fmt::format("processing_bin_{}", index);
	instance_bin->bin = gst_bin_new(elem_name.c_str());

	if(!create_sink_bin(config.num_sink_sub_bins, config.sink_bin_sub_bin_configs, &instance_bin->sink, index))
	{
		goto done;
	}

	gst_bin_add(GST_BIN(instance_bin->bin), instance_bin->sink.bin);
	last_elem = instance_bin->sink.bin;

	if(config.osd_config.enable)
	{
		OSDBin *osd_bin{ &instance_bin->osd };
		if(!create_osd_bin(&config.osd_config, osd_bin))
		{
			goto done;
		}

		gst_bin_add(GST_BIN(instance_bin->bin), osd_bin->bin);

		TADS_LINK_ELEMENT(osd_bin->bin, last_elem);

		last_elem = osd_bin->bin;
	}

	TADS_BIN_ADD_GHOST_PAD(instance_bin->bin, last_elem, "sink");
	if(config.osd_config.enable)
	{
		TADS_ELEM_ADD_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->osd.nvosd, "sink",
												gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
	}
	else
	{
		TADS_ELEM_ADD_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->sink.bin, "sink",
												gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantParameter"
bool AppContext::create_demux_pipeline(uint index)
{
	GstElement *last_elem;
	std::string elem_name;

	bool success{};
	InstanceBin *instance_bin{ &this->pipeline.demux_instance_bins.at(index) };

	instance_bin->index = index;
	instance_bin->app_ctx = this;

	elem_name = fmt::format("processing_demux_bin_{}", index);
	instance_bin->bin = gst_bin_new(elem_name.c_str());

	if(!create_demux_sink_bin(config.num_sink_sub_bins, config.sink_bin_sub_bin_configs, &instance_bin->demux_sink,
														config.sink_bin_sub_bin_configs[index].source_id))
	{
		goto done;
	}

	gst_bin_add(GST_BIN(instance_bin->bin), instance_bin->demux_sink.bin);
	last_elem = instance_bin->demux_sink.bin;

	if(config.osd_config.enable)
	{
		if(!create_osd_bin(&config.osd_config, &instance_bin->osd))
		{
			goto done;
		}

		gst_bin_add(GST_BIN(instance_bin->bin), instance_bin->osd.bin);

		TADS_LINK_ELEMENT(instance_bin->osd.bin, last_elem);

		last_elem = instance_bin->osd.bin;
	}

	TADS_BIN_ADD_GHOST_PAD(instance_bin->bin, last_elem, "sink");
	if(config.osd_config.enable)
	{
		TADS_ELEM_ADD_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->osd.nvosd, "sink",
												gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
	}
	else
	{
		TADS_ELEM_ADD_PROBE(instance_bin->all_bbox_buffer_probe_id, instance_bin->demux_sink.bin, "sink",
												gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}
#pragma clang diagnostic pop

#pragma clang diagnostic pop
