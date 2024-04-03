#include <cstring>
#include <fstream>

#include "secondary_gie.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
#pragma ide diagnostic ignored "misc-no-recursion"
#define TADS_GET_FILE_PATH(path) ((path) + (((path) && strstr((path), "file://")) ? 7 : 0))

/**
 * Wait for all secondary inferences to complete the processing and then send
 * the processed buffer to downstream.
 * This is way of synchronization between all secondary infers and sending
 * buffer once meta data from all secondary infer components got attached.
 * This is needed because all secondary infers process same buffer in parallel.
 */
static GstPadProbeReturn wait_queue_buf_probe(GstPad *, GstPadProbeInfo *info, void *data)
{
	auto *bin = reinterpret_cast<SecondaryGieBin *>(data);
	if(info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)
	{
		auto *event = reinterpret_cast<GstEvent *>(info->data);
		if(event->type == GST_EVENT_EOS)
		{
			return GST_PAD_PROBE_OK;
		}
	}

	if(info->type & GST_PAD_PROBE_TYPE_BUFFER)
	{
		g_mutex_lock(&bin->wait_lock);
		while(GST_OBJECT_REFCOUNT_VALUE(GST_BUFFER(info->data)) > 1 && !bin->stop && !bin->flush)
		{
			gint64 end_time;
			end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND / 1000;
			g_cond_wait_until(&bin->wait_cond, &bin->wait_lock, end_time);
		}
		g_mutex_unlock(&bin->wait_lock);
	}

	return GST_PAD_PROBE_OK;
}

/**
 * Probe function on sink pad of tee element. It is being used to
 * capture EOS event. So that wait for all secondary to finish can be stopped.
 * see ::wait_queue_buf_probe
 */
static GstPadProbeReturn wait_queue_buf_probe1(GstPad *, GstPadProbeInfo *info, void *data)
{
	auto *bin = reinterpret_cast<SecondaryGieBin *>(data);
	if(info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)
	{
		auto *event = reinterpret_cast<GstEvent *>(info->data);
		if(event->type == GST_EVENT_EOS)
		{
			bin->stop = true;
		}
	}

	return GST_PAD_PROBE_OK;
}

static void write_infer_output_to_file(GstBuffer *, NvDsInferNetworkInfo *, NvDsInferLayerInfo *layers_info,
																			 uint num_layers, uint batch_size, void *data)
{
	auto *config = reinterpret_cast<GieConfig *>(data);
	uint i;

	/* "gst_buffer_get_nvstream_meta()" can be called on the GstBuffer to get more
	 * information about the buffer.*/

	for(i = 0; i < num_layers; i++)
	{
		NvDsInferLayerInfo *info = &layers_info[i];
		uint element_size{};
		FILE *file;
		std::string file_name;
		std::string layer_name;

		switch(info->dataType)
		{
			case FLOAT:
				element_size = 4;
				break;
			case HALF:
				element_size = 2;
				break;
			case INT32:
				element_size = 4;
				break;
			case INT8:
				element_size = 1;
				break;
		}

		layer_name = info->layerName;
		for(auto &ch : layer_name)
		{
			if(ch == '/')
				ch = '_';
		}

		file_name = fmt::format("{}/{}_batch{:010}_batchsize{:02}.bin", config->raw_output_directory, layer_name,
														config->file_write_frame_num, batch_size);

		file = fopen(file_name.c_str(), "w");
		if(!file)
		{
			TADS_ERR_MSG_V("Could not open file '%s' for writing:%s", file_name.c_str(), strerror(errno));
			continue;
		}
		fwrite(info->buffer, element_size, info->inferDims.numElements * batch_size, file);
		fclose(file);
	}
	config->file_write_frame_num++;
}

/**
 * This decides if secondary infer sub bin should be created or not.
 * Decision is based on following criteria.
 * 1) It is enabled in configuration file.
 * 2) operate_on_gie_id should match the provided unique id of primary infer.
 * 3) If third or more level of inference is created then operate_on_gie_id
 *    and unique_id should be created in such a way that third infer operate
 *    on second's output and second operate on primary's output and so on.
 */
static bool should_create_secondary_gie(const std::vector<GieConfig> &configs, uint num_configs,
																				std::vector<SecondaryGieBinSubBin> &sub_bins, uint index, int primary_gie_id)
{
	const GieConfig *config{ &configs.at(index) };
	SecondaryGieBinSubBin *sub_bin{ &sub_bins.at(index) };

	if(!config->enable)
	{
		return false;
	}

	if(sub_bin->create)
	{
		return true;
	}

	if(config->operate_on_gie_id == primary_gie_id)
	{
		sub_bin->create = true;
		sub_bin->parent_index = -1;
		return true;
	}

	for(uint i{}; i < num_configs; i++)
	{
		if(config->operate_on_gie_id == configs.at(i).unique_id)
		{
			if(should_create_secondary_gie(configs, num_configs, sub_bins, i, primary_gie_id))
			{
				sub_bin->create = true;
				sub_bin->parent_index = i;
				sub_bins.at(i).num_children++;
				return true;
			}
			break;
		}
	}
	return false;
}
#pragma clang diagnostic pop

/**
 * Create secondary infer sub bin and sets properties mentioned
 * in configuration file.
 *
 * @param[in] configs array of pointers of type @ref NvDsGieConfig
 *            parsed from configuration file.
 * @param[in] sub_bins secondary gie sub bins.
 * @param[in] bin Unique id of primary infer to work on.
 * @param[in] index Index of the sub bin.
 */
static bool create_secondary_gie_subbin(const std::vector<GieConfig> &configs,
																				std::vector<SecondaryGieBinSubBin> &sub_bins, GstBin *bin, uint index)
{
	bool success{};
	std::string elem_name;
	std::string factory_name;
	std::string operate_on_class_str, filter_out_class_str;
	const GieConfig *config{ &configs.at(index) };
	SecondaryGieBinSubBin *sub_bin = &sub_bins.at(index);
	gst_nvinfer_raw_output_generated_callback out_callback = write_infer_output_to_file;
	int process_mode = static_cast<int>(GieProcessMode::SECONDARY);
	const char *config_file_path;

	if(!sub_bin->create)
	{
		return true;
	}

	elem_name = fmt::format("secondary_gie_{}_queue", index);
	if(sub_bin->parent_index == -1 || sub_bins[sub_bin->parent_index].num_children > 1)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Creating queue element '%s'", elem_name.c_str());
#endif
		sub_bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
		if(!sub_bin->queue)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(bin, sub_bin->queue);
	}

	elem_name = fmt::format("secondary_gie_{}", index);
	switch(config->plugin_type)
	{
		case GiePluginType::INFER:
			factory_name = TADS_ELEM_NVINFER;
			break;
		case GiePluginType::INFER_SERVER:
			factory_name = TADS_ELEM_NVINFER_SERVER;
			break;
		default:
			TADS_ERR_MSG_V("Failed to create %s on unknown plugin_type", elem_name.c_str());
			goto done;
	}

#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating %s element '%s'", factory_name.c_str(), elem_name.c_str());
#endif
	sub_bin->gie = gst::element_factory_make(factory_name, elem_name);
	if(!sub_bin->gie)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	for(int class_id : config->operate_on_classes)
	{
		operate_on_class_str.append(fmt::format("{}:", class_id));
	}

	for(int class_id : config->filter_out_classes)
	{
		filter_out_class_str.append(fmt::format("{}:", class_id));
	}

	config_file_path = TADS_GET_FILE_PATH(config->config_file_path.c_str());

#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s properties 'config-file-path'=%s, 'process-mode'=%d", factory_name.c_str(),
								 config_file_path, process_mode);
#endif
	g_object_set(G_OBJECT(sub_bin->gie), "config-file-path", config_file_path, "process-mode", process_mode, nullptr);

#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s property 'input-tensor-meta'=%d", factory_name.c_str(), config->input_tensor_meta);
#endif
	g_object_set(G_OBJECT(sub_bin->gie), "input-tensor-meta", config->input_tensor_meta, nullptr);

	if(!operate_on_class_str.empty())
	{
		operate_on_class_str.erase(operate_on_class_str.length() - 1);

#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'infer-on-class-ids'=%s", factory_name.c_str(), operate_on_class_str.c_str());
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "infer-on-class-ids", operate_on_class_str.c_str(), nullptr);
	}

	if(!filter_out_class_str.empty())
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'filter-out-class-ids'=%s", factory_name.c_str(), filter_out_class_str.c_str());
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "filter-out-class-ids", filter_out_class_str.c_str(), nullptr);
	}

	if(config->is_batch_size_set)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'batch-size'=%d", factory_name.c_str(), config->batch_size);
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "batch-size", config->batch_size, nullptr);
	}

	if(config->is_interval_set)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'interval'=%d", factory_name.c_str(), config->interval);
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "interval", config->interval, nullptr);
	}

	if(config->is_unique_id_set)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'unique-id'=%d", factory_name.c_str(), config->unique_id);
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "unique-id", config->unique_id, nullptr);
	}

	if(config->is_operate_on_gie_id_set)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'infer-on-gie-id'=%d", factory_name.c_str(), config->operate_on_gie_id);
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "infer-on-gie-id", config->operate_on_gie_id, nullptr);
	}

	if(!config->raw_output_directory.empty())
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'raw-output-generated-callback'", factory_name.c_str());
#endif
		g_object_set(G_OBJECT(sub_bin->gie), "raw-output-generated-callback", out_callback, "raw-output-generated-userdata",
								 config, nullptr);
	}

	if(config->is_gpu_id_set && config->plugin_type == GiePluginType::INFER_SERVER)
	{
		TADS_WARN_MSG_V("gpu-id: %u in sgie: %s is ignored, only accept nvinferserver config", config->gpu_id,
										elem_name.c_str());
	}

	if(config->plugin_type == GiePluginType::INFER)
	{
		if(!config->model_engine_file_path.empty())
		{
			const char *model_engine_file_path = config->model_engine_file_path.c_str();
#ifdef TADS_SECONDARY_GIE_DEBUG
			TADS_DBG_MSG_V("Setting %s property 'model-engine-file'=%s", factory_name.c_str(), model_engine_file_path);
#endif
			g_object_set(G_OBJECT(sub_bin->gie), "model-engine-file", TADS_GET_FILE_PATH(model_engine_file_path), nullptr);
		}

		if(config->is_gpu_id_set)
		{
#ifdef TADS_SECONDARY_GIE_DEBUG
			TADS_DBG_MSG_V("Setting %s property 'gpu-id'=%d", factory_name.c_str(), config->gpu_id);
#endif
			g_object_set(G_OBJECT(sub_bin->gie), "gpu-id", config->gpu_id, nullptr);
		}
	}

	gst_bin_add(bin, sub_bin->gie);

	if(sub_bin->num_children == 0)
	{
		bool async{}, sync{};
		bool enable_last_sample{};
		elem_name = fmt::format("secondary_gie_{}_sink", index);

#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Creating sink element '%s'", elem_name.c_str());
#endif
		sub_bin->sink = gst::element_factory_make(TADS_ELEM_SINK_FAKESINK, elem_name);
		if(!sub_bin->sink)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(bin, sub_bin->sink);

#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting sink properties 'async'=%d, 'sync'=%d, 'enable-last-sample'=%d", async, sync,
									 enable_last_sample);
#endif
		g_object_set(G_OBJECT(sub_bin->sink), "async", async, "sync", sync, "enable-last-sample", enable_last_sample,
								 nullptr);
	}

	if(sub_bin->num_children > 1)
	{
		elem_name = fmt::format("secondary_gie_{}_tee", index);
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Creating tee element '%s'", elem_name.c_str());
#endif
		sub_bin->tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
		if(!sub_bin->tee)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(bin, sub_bin->tee);
	}

	if(sub_bin->queue)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Linking queue element with %s element", factory_name.c_str());
#endif
		TADS_LINK_ELEMENT(sub_bin->queue, sub_bin->gie);
	}
	if(sub_bin->sink)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Linking %s element with sink element", factory_name.c_str());
#endif
		TADS_LINK_ELEMENT(sub_bin->gie, sub_bin->sink);
	}
	if(sub_bin->tee)
	{
#ifdef TADS_SECONDARY_GIE_DEBUG
		TADS_DBG_MSG_V("Linking %s element with tee element", factory_name.c_str());
#endif
		TADS_LINK_ELEMENT(sub_bin->gie, sub_bin->tee);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool create_secondary_gie(uint num_secondary_gie, uint primary_gie_unique_id, const std::vector<GieConfig> &configs,
													SecondaryGieBin *bin)
{
	bool success{};
	uint i;
	GstPad *pad;
	SecondaryGieBinSubBin *sub_bin;
	std::string elem_name{ "bin" };

#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating bin element '%s'", elem_name.c_str());
#endif
	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "secondary_gie_bin_tee";
#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating tee element '%s'", elem_name.c_str());
#endif
	bin->tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!bin->tee)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->tee);

	elem_name = "secondary_gie_bin_queue";
#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating queue element '%s'", elem_name.c_str());
#endif
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->queue);

	pad = gst_element_get_static_pad(bin->queue, "src");
	bin->wait_for_sgie_process_buf_probe_id =
			gst_pad_add_probe(pad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_BOTH),
												wait_queue_buf_probe, bin, nullptr);
	gst_object_unref(pad);
	pad = gst_element_get_static_pad(bin->tee, "sink");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_BOTH, wait_queue_buf_probe1, bin, nullptr);
	gst_object_unref(pad);

#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Add ghost pad bin element with tee element sink pad");
#endif
	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->tee, "sink");
#ifdef TADS_SECONDARY_GIE_DEBUG
	TADS_DBG_MSG_V("Add ghost pad bin element with queue element src pad");
#endif
	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "src");

	if(!gst::link_element_to_tee_src_pad(bin->tee, bin->queue))
	{
		goto done;
	}

	for(i = 0; i < num_secondary_gie; i++)
	{
		should_create_secondary_gie(configs, num_secondary_gie, bin->sub_bins, i, primary_gie_unique_id);
	}

	for(i = 0; i < num_secondary_gie; i++)
	{
		if(bin->sub_bins.at(i).create)
		{
			if(!create_secondary_gie_subbin(configs, bin->sub_bins, GST_BIN(bin->bin), i))
			{
				goto done;
			}
		}
	}

	for(i = 0; i < num_secondary_gie; i++)
	{
		sub_bin = &bin->sub_bins.at(i);
		if(sub_bin->create)
		{
			int parent_index{ sub_bin->parent_index };
			if(parent_index == -1)
			{
				gst::link_element_to_tee_src_pad(bin->tee, sub_bin->queue);
			}
			else
			{
				if(bin->sub_bins[parent_index].tee)
				{
					gst::link_element_to_tee_src_pad(bin->sub_bins[parent_index].tee, sub_bin->queue);
				}
				else
				{
					TADS_LINK_ELEMENT(bin->sub_bins[parent_index].gie, sub_bin->gie);
				}
			}
		}
	}

	g_mutex_init(&bin->wait_lock);
	g_cond_init(&bin->wait_cond);

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

[[maybe_unused]]
void destroy_secondary_gie(SecondaryGieBin *bin)
{
	if(bin->queue && bin->wait_for_sgie_process_buf_probe_id)
	{
		GstPad *pad = gst_element_get_static_pad(bin->queue, "src");
		gst_pad_remove_probe(pad, bin->wait_for_sgie_process_buf_probe_id);
		gst_object_unref(pad);
	}
}