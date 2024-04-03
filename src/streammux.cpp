#include <cstring>

#include "streammux.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
// Create bin, add queue and the element, link all elements and ghost pads,
// Set the element properties from the parsed config
bool set_streammux_properties(StreammuxConfig *config, GstElement *element)
{
	bool success{ true };
	std::string new_mux_str{ g_getenv("USE_NEW_NVSTREAMMUX") != nullptr ? g_getenv("USE_NEW_NVSTREAMMUX") : "no" };
	bool use_new_mux = new_mux_str == "yes";

	if(!use_new_mux)
	{
		g_object_set(G_OBJECT(element), "gpu-id", config->gpu_id, nullptr);

		g_object_set(G_OBJECT(element), "nvbuf-memory-type", config->nvbuf_memory_type, nullptr);

		g_object_set(G_OBJECT(element), "live-source", config->live_source, nullptr);

		g_object_set(G_OBJECT(element), "batched-push-timeout", config->batched_push_timeout, nullptr);

		if(config->buffer_pool_size >= 4)
		{
			g_object_set(G_OBJECT(element), "buffer-pool-size", config->buffer_pool_size, nullptr);
		}

		g_object_set(G_OBJECT(element), "enable-padding", config->enable_padding, nullptr);

		if(config->width && config->height)
		{
			g_object_set(G_OBJECT(element), "width", config->width, nullptr);
			g_object_set(G_OBJECT(element), "height", config->height, nullptr);
		}
		if(!config->use_nvmultiurisrcbin)
		{
			g_object_set(G_OBJECT(element), "async-process", config->async_process, nullptr);
		}
	}

	if(config->batch_size && !config->use_nvmultiurisrcbin)
	{
		g_object_set(G_OBJECT(element), "batch-size", config->batch_size, nullptr);
	}

	g_object_set(G_OBJECT(element), "attach-sys-ts", config->attach_sys_ts_as_ntp, nullptr);

	if(!config->config_file_path.empty())
	{
		g_object_set(G_OBJECT(element), "config-file-path", TADS_GET_FILE_PATH(config->config_file_path.c_str()), nullptr);
	}

	g_object_set(G_OBJECT(element), "frame-duration", config->frame_duration, nullptr);

	g_object_set(G_OBJECT(element), "frame-num-reset-on-stream-reset", config->frame_num_reset_on_stream_reset, nullptr);

	g_object_set(G_OBJECT(element), "sync-inputs", config->sync_inputs, nullptr);

	g_object_set(G_OBJECT(element), "max-latency", config->max_latency, nullptr);
	g_object_set(G_OBJECT(element), "frame-num-reset-on-eos", config->frame_num_reset_on_eos, nullptr);
	g_object_set(G_OBJECT(element), "drop-pipeline-eos", config->no_pipeline_eos, nullptr);

	if(config->num_surface_per_frame > 1)
	{
		g_object_set(G_OBJECT(element), "num-surfaces-per-frame", config->num_surface_per_frame, nullptr);
	}

	return success;
}
#pragma clang diagnostic pop
