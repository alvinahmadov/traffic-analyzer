#include "tracker.hpp"

GST_DEBUG_CATEGORY_EXTERN(NVDS_APP);

bool create_tracking_bin(TrackerConfig *config, TrackerBin *bin)
{
	bool success{};
	std::string elem_name{ "tracking_bin" };
	int compute_hw = static_cast<int >(config->compute_hw);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Creating bin element '%s'", elem_name.c_str());
#endif
	bin->bin = gst::bin_new(elem_name);
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "tracking_tracker";
#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Creating tracker element '%s'", elem_name.c_str());
#endif
	bin->tracker = gst::element_factory_make(TADS_ELEM_TRACKER, elem_name);
	if(!bin->tracker)
	{
		TADS_ERR_MSG_V("Failed to create 'tracking_tracker'");
		goto done;
	}

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V(
			"Setting tracker properties tracker-width=%d, tracker-height=%d, gpu-id=%d, ll-config-file=%s, ll-lib-file=%s",
			config->tracker_width, config->tracker_height, config->gpu_id, config->ll_config_file.c_str(),
			config->ll_lib_file.c_str());
#endif
	g_object_set(G_OBJECT(bin->tracker), "tracker-width", config->tracker_width, "tracker-height",
							 config->tracker_height, "gpu-id", config->gpu_id, "ll-config-file", config->ll_config_file.c_str(),
							 "ll-lib-file", config->ll_lib_file.c_str(), nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'display-tracking-id'=%d", config->display_tracking_id);
#endif
	g_object_set(G_OBJECT(bin->tracker), "display-tracking-id", config->display_tracking_id, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'tracking-id-reset-mode'=%u", config->tracking_id_reset_mode);
#endif
	g_object_set(G_OBJECT(bin->tracker), "tracking-id-reset-mode", config->tracking_id_reset_mode, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'tracking-surface-type'=%u", config->tracking_surface_type);
#endif
	g_object_set(G_OBJECT(bin->tracker), "tracking-surface-type", config->tracking_surface_type, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'input-tensor-meta'=%d", config->input_tensor_meta);
#endif
	g_object_set(G_OBJECT(bin->tracker), "input-tensor-meta", config->input_tensor_meta, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'tensor-meta-gie-id'=%d", config->input_tensor_gie_id);
#endif
	g_object_set(G_OBJECT(bin->tracker), "tensor-meta-gie-id", config->input_tensor_gie_id, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'compute-hw'=%d", compute_hw);
#endif
	g_object_set(G_OBJECT(bin->tracker), "compute-hw", compute_hw, nullptr);

#ifdef TADS_TRACKER_DEBUG
	TADS_DBG_MSG_V("Setting tracker property 'user-meta-pool-size'=%d", config->user_meta_pool_size);
#endif
	g_object_set(G_OBJECT(bin->tracker), "user-meta-pool-size", config->user_meta_pool_size, nullptr);
	if(!config->sub_batches.empty())
	{
#ifdef TADS_TRACKER_DEBUG
		TADS_DBG_MSG_V("Setting tracker property 'sub-batches'=%s", config->sub_batches.c_str());
#endif
		g_object_set(G_OBJECT(bin->tracker), "sub-batches", config->sub_batches.c_str(), nullptr);
	}

	gst_bin_add_many(GST_BIN(bin->bin), bin->tracker, nullptr);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->tracker, "sink");

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->tracker, "src");

	success = true;

	#ifdef TADS_TRACKER_DEBUG
		TADS_DBG_MSG_V("Tracker created successfully");
	#endif
//	GST_CAT_DEBUG(NVDS_APP, "Tracker bin created successfully");

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}
