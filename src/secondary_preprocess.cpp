#include "secondary_preprocess.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"

/**
 * Wait for all secondary preprocess to complete the processing and then send
 * the processed buffer to downstream.
 * This is way of synchronization between all secondary preprocess and sending
 * buffer once meta data from all secondary infer components got attached.
 * This is needed because all secondary preprocess process same buffer in parallel.
 */
static GstPadProbeReturn wait_queue_buf_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *bin = reinterpret_cast<SecondaryPreProcessBin *>(data);
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
static GstPadProbeReturn wait_queue_buf_probe1([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *bin = reinterpret_cast<SecondaryPreProcessBin *>(data);
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

static bool create_secondary_preprocess(const std::vector<PreProcessConfig> &configs,
																				std::vector<SecondaryPreProcessSubBin> &sub_bins, GstBin *bin, uint index)
{
	bool success{};
	std::string elem_name;
	const PreProcessConfig *config{ &configs.at(index) };
	SecondaryPreProcessSubBin *sub_bin{ &sub_bins[index] };

	if(!sub_bin->create)
	{
		return true;
	}

	elem_name = fmt::format("secondary_preprocess_{}_queue", index);
	if(sub_bin->parent_index == -1 || sub_bins[sub_bin->parent_index].num_children > 1)
	{
		sub_bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
		if(!sub_bin->queue)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(bin, sub_bin->queue);
	}

	elem_name = fmt::format("secondary_preprocess_{}", index);
	sub_bin->secondary_preprocess = gst::element_factory_make(TADS_ELEM_SECONDARY_PREPROCESS, elem_name);

	if(!sub_bin->secondary_preprocess)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(sub_bin->secondary_preprocess), "config-file", config->config_file_path.c_str(), nullptr);

	if(config->is_operate_on_gie_id_set)
	{
		g_object_set(G_OBJECT(sub_bin->secondary_preprocess), "operate-on-gie-id", config->operate_on_gie_id, nullptr);
	}

	gst_bin_add(bin, sub_bin->secondary_preprocess);

	if(sub_bin->num_children == 0)
	{
		elem_name = fmt::format("secondary_preprocess_{}_sink", index);
		sub_bin->sink = gst::element_factory_make(TADS_ELEM_SINK_FAKESINK, elem_name);
		if(!sub_bin->sink)
		{
			TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
			goto done;
		}
		gst_bin_add(bin, sub_bin->sink);
		g_object_set(G_OBJECT(sub_bin->sink), "async", false, "sync", false, "enable-last-sample", false, nullptr);
	}

	if(sub_bin->num_children > 1)
	{
		elem_name = fmt::format("secondary_preprocess_{}_tee", index);
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
		TADS_LINK_ELEMENT(sub_bin->queue, sub_bin->secondary_preprocess);
	}
	if(sub_bin->sink)
	{
		TADS_LINK_ELEMENT(sub_bin->secondary_preprocess, sub_bin->sink);
	}
	if(sub_bin->tee)
	{
		TADS_LINK_ELEMENT(sub_bin->secondary_preprocess, sub_bin->tee);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
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
static bool should_create_secondary_preprocess(const PreProcessConfig *config, SecondaryPreProcessSubBin *sub_bin,
																							 int primary_gie_id)
{
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

	return false;
}

bool create_secondary_preprocess_bin(uint num_secondary_preprocess, uint primary_gie_unique_id,
																		 const std::vector<PreProcessConfig> &configs, SecondaryPreProcessBin *bin)
{
	bool success{};
	uint i;
	GstPad *pad;
	SecondaryPreProcessSubBin *sub_bin;
	std::string elem_name{ "secondary_preprocess_bin" };

	bin->bin = gst_bin_new(elem_name.c_str());
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "secondary_preprocess_bin_tee";
	bin->tee = gst::element_factory_make(TADS_ELEM_TEE, elem_name);
	if(!bin->tee)
	{
		TADS_ERR_MSG_V("Failed to create element '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->tee);

	elem_name = "secondary_preprocess_queue";
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add(GST_BIN(bin->bin), bin->queue);

	pad = gst_element_get_static_pad(bin->queue, "src");
	bin->wait_for_secondary_preprocess_process_buf_probe_id =
			gst_pad_add_probe(pad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_BOTH),
												wait_queue_buf_probe, bin, nullptr);
	gst_object_unref(pad);
	pad = gst_element_get_static_pad(bin->tee, "sink");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_BOTH, wait_queue_buf_probe1, bin, nullptr);
	gst_object_unref(pad);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->tee, "sink");
	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "src");

	if(!gst::link_element_to_tee_src_pad(bin->tee, bin->queue))
	{
		goto done;
	}

	for(i = 0; i < num_secondary_preprocess; i++)
	{
		auto *config{ &configs.at(i) };
		sub_bin = &bin->sub_bins.at(i);
		should_create_secondary_preprocess(config, sub_bin, primary_gie_unique_id);
		if(sub_bin->create)
		{
			if(!create_secondary_preprocess(configs, bin->sub_bins, GST_BIN(bin->bin), i))
			{
				goto done;
			}
		}
	}

	for(i = 0; i < num_secondary_preprocess; i++)
	{
		sub_bin = &bin->sub_bins.at(i);
		int parent_index{ sub_bin->parent_index };
		if(sub_bin->create)
		{
			if(parent_index == -1)
			{
				gst::link_element_to_tee_src_pad(bin->tee, sub_bin->queue);
			}
			else
			{
				auto parent_bin{ &bin->sub_bins[parent_index] };
				if(parent_bin->tee)
				{
					gst::link_element_to_tee_src_pad(parent_bin->tee, sub_bin->queue);
				}
				else
				{
					TADS_LINK_ELEMENT(parent_bin->secondary_preprocess, sub_bin->secondary_preprocess);
				}
			}
		}
	}

	g_mutex_init(&bin->wait_lock);
	g_cond_init(&bin->wait_cond);

	success = TRUE;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return success;
}

[[maybe_unused]]
void destroy_secondary_preprocess_bin(SecondaryPreProcessBin *bin)
{
	if(bin->queue && bin->wait_for_secondary_preprocess_process_buf_probe_id)
	{
		GstPad *pad = gst_element_get_static_pad(bin->queue, "src");
		gst_pad_remove_probe(pad, bin->wait_for_secondary_preprocess_process_buf_probe_id);
		gst_object_unref(pad);
	}
}

#pragma clang diagnostic pop