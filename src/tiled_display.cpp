#include "tiled_display.hpp"

bool create_tiled_display_bin(TiledDisplayConfig *config, TiledDisplayBin *bin)
{
	bool success{};
	std::string elem_name{ "tiled_display_bin" };

	bin->bin = gst_bin_new(elem_name.c_str());
	if(!bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "tiled_display_queue";
	bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(config->enable == TiledDisplayState::ENABLED || config->enable == TiledDisplayState::ENABLED_WITH_PARALLEL_DEMUX)
	{
		elem_name = "tiled_display_tiler";
		bin->tiler = gst::element_factory_make(TADS_ELEM_TILER, elem_name);
	}
	// TODO: Inspect NvDsTiledDisplayState::3
	else if(config->enable == TiledDisplayState::DISABLED)
	{
		elem_name = "tiled_display_identity";
		bin->tiler = gst::element_factory_make(TADS_ELEM_IDENTITY, elem_name);
	}

	if(!bin->tiler)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	if(config->width)
		g_object_set(G_OBJECT(bin->tiler), "width", config->width, nullptr);

	if(config->height)
		g_object_set(G_OBJECT(bin->tiler), "height", config->height, nullptr);

	if(config->rows)
		g_object_set(G_OBJECT(bin->tiler), "rows", config->rows, nullptr);

	if(config->columns)
		g_object_set(G_OBJECT(bin->tiler), "columns", config->columns, nullptr);

	if(config->buffer_pool_size)
		g_object_set(G_OBJECT(bin->tiler), "buffer-pool-size", config->buffer_pool_size, nullptr);

#ifdef IS_TEGRA
	if(config->compute_hw)
		g_object_set(G_OBJECT(bin->tiler), "compute-hw", config->compute_hw, nullptr);
#endif

	g_object_set(G_OBJECT(bin->tiler), "gpu-id", config->gpu_id, "nvbuf-memory-type", config->nvbuf_memory_type, nullptr);

	gst_bin_add_many(GST_BIN(bin->bin), bin->queue, bin->tiler, nullptr);

	TADS_LINK_ELEMENT(bin->queue, bin->tiler);

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->queue, "sink");

	TADS_BIN_ADD_GHOST_PAD(bin->bin, bin->tiler, "src");

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}
