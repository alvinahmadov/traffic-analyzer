#include <cstring>
#include <fstream>

#include "primary_gie.hpp"

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
		uint j;
		FILE *file;
		std::string file_name;
		char layer_name[128];

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

		g_strlcpy(layer_name, info->layerName, 128);
		for(j = 0; layer_name[j] != '\0'; j++)
		{
			layer_name[j] = (layer_name[j] == '/') ? '_' : layer_name[j];
		}

		file_name = fmt::format("{}/{}_batch{:010}_batchsize{:02}.bin", config->raw_output_directory, layer_name,
														config->file_write_frame_num, batch_size);

		file = fopen(file_name.c_str(), "wb");
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

bool create_primary_gie(GieConfig *config, PrimaryGieBin *bin)
{
	bool success{};
	int process_mode{ static_cast<int>(GieProcessMode::PRIMARY) };
	gst_nvinfer_raw_output_generated_callback out_callback = write_infer_output_to_file;
	std::string elem_name{ "primary_gie" };
	std::string factory_name;
	const char *config_file_path;
	int nvbuf_memory_type = static_cast<int>(config->nvbuf_memory_type);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating bin element '%s'", elem_name.c_str());
#endif

	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "primary_gie_conv";

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating nvvidconv element '%s'", elem_name.c_str());
#endif
	bin->nvvidconv = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!bin->nvvidconv)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "primary_gie_queue";

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating queue element '%s'", elem_name.c_str());
#endif
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "primary_gie";
	switch(config->plugin_type)
	{
		case GiePluginType::INFER:
			factory_name = TADS_ELEM_NVINFER;
			break;
		case GiePluginType::INFER_SERVER:
			factory_name = TADS_ELEM_NVINFER_SERVER;
			break;
		default:
			TADS_ERR_MSG_V("Failed to create '%s' on unknown plugin_type", elem_name.c_str());
			goto done;
	}

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Creating %s element '%s'", factory_name.c_str(), elem_name.c_str());
#endif
	bin->gie = gst::element_factory_make(factory_name, elem_name);
	if(!bin->gie)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	config_file_path = TADS_GET_FILE_PATH(config->config_file_path.c_str());

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s properties 'config-file-path'=%s, 'process-mode'=%d", factory_name.c_str(),
								 config_file_path, process_mode);
#endif
	g_object_set(G_OBJECT(bin->gie), "config-file-path", config_file_path, "process-mode", process_mode, nullptr);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s property 'input-tensor-meta'=%d", factory_name.c_str(), config->input_tensor_meta);
#endif
	g_object_set(G_OBJECT(bin->gie), "input-tensor-meta", config->input_tensor_meta, nullptr);

	if(config->is_batch_size_set)
	{
#ifdef TADS_PRIMARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'batch-size'=%d", factory_name.c_str(), config->batch_size);
#endif
		g_object_set(G_OBJECT(bin->gie), "batch-size", config->batch_size, nullptr);
	}

	if(config->is_interval_set)
	{
#ifdef TADS_PRIMARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'interval'=%d", factory_name.c_str(), config->interval);
#endif
		g_object_set(G_OBJECT(bin->gie), "interval", config->interval, nullptr);
	}

	if(config->is_unique_id_set)
	{
#ifdef TADS_PRIMARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'unique-id'=%d", factory_name.c_str(), config->unique_id);
#endif
		g_object_set(G_OBJECT(bin->gie), "unique-id", config->unique_id, nullptr);
	}

	if(config->is_gpu_id_set && config->plugin_type == GiePluginType::INFER_SERVER)
	{
		TADS_INFO_MSG_V("gpu-id: %u in primary-gie group is ignored, only accept in nvinferserver's config",
										config->gpu_id);
	}

	if(!config->raw_output_directory.empty())
	{
#ifdef TADS_PRIMARY_GIE_DEBUG
		TADS_DBG_MSG_V("Setting %s property 'raw-output-generated-callback'", factory_name.c_str());
#endif
		g_object_set(G_OBJECT(bin->gie), "raw-output-generated-callback", out_callback, "raw-output-generated-userdata",
								 config, nullptr);
	}

	if(config->plugin_type == GiePluginType::INFER)
	{
		if(config->is_gpu_id_set)
		{
#ifdef TADS_PRIMARY_GIE_DEBUG
			TADS_DBG_MSG_V("Setting %s property 'gpu-id'=%d", factory_name.c_str(), config->gpu_id);
#endif
			g_object_set(G_OBJECT(bin->gie), "gpu-id", config->gpu_id, nullptr);
		}

		if(!config->model_engine_file_path.empty())
		{
			const char *model_engine_file_path = TADS_GET_FILE_PATH(config->model_engine_file_path.c_str());
#ifdef TADS_PRIMARY_GIE_DEBUG
			TADS_DBG_MSG_V("Setting %s property 'model-engine-file'=%s", factory_name.c_str(), model_engine_file_path);
#endif
			g_object_set(G_OBJECT(bin->gie), "model-engine-file", model_engine_file_path, nullptr);
		}
	}

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s property 'gpu-id'=%d", GST_ELEMENT_NAME(bin->nvvidconv), config->gpu_id);
#endif
	g_object_set(G_OBJECT(bin->nvvidconv), "gpu-id", config->gpu_id, nullptr);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Setting %s property 'nvbuf-memory-type'=%d", GST_ELEMENT_NAME(bin->nvvidconv), nvbuf_memory_type);
#endif
	g_object_set(G_OBJECT(bin->nvvidconv), "nvbuf-memory-type", nvbuf_memory_type, nullptr);

	gst_bin_add_many(GST_BIN(bin->bin), bin->queue, bin->nvvidconv, bin->gie, nullptr);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Linking queue element with 'nvvidconv' element");
#endif
	TADS_LINK_ELEMENT(bin->queue, bin->nvvidconv);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Linking nvvidconv element with 'primary_gie' element");
#endif
	TADS_LINK_ELEMENT(bin->nvvidconv, bin->gie);

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Add ghost pad bin element with primary_gie element src pad");
#endif
	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->gie, "src");

#ifdef TADS_PRIMARY_GIE_DEBUG
	TADS_DBG_MSG_V("Add ghost pad bin element with queue element sink pad");
#endif
	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}
