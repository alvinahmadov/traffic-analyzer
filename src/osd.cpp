#include "osd.hpp"

bool create_osd_bin(OSDConfig *config, OSDBin *osd_bin)
{
	bool success{};
	uint clk_color = ((static_cast<uint>((config->clock_color.red * 255)) & 0xFF) << 24) |
									 ((static_cast<uint>((config->clock_color.green * 255)) & 0xFF) << 16) |
									 ((static_cast<uint>((config->clock_color.blue * 255)) & 0xFF) << 8) | 0xFF;
	std::string elem_name{ "osd_bin" };

	osd_bin->bin = gst::bin_new(elem_name);
	if(!osd_bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "osd_conv";
	osd_bin->nvvidconv = gst::element_factory_make(TADS_ELEM_NVVIDEO_CONV, elem_name);
	if(!osd_bin->nvvidconv)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "osd_queue";
	osd_bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!osd_bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "osd_conv_queue";
	osd_bin->conv_queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!osd_bin->conv_queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "nvosd0";
	osd_bin->nvosd = gst::element_factory_make(TADS_ELEM_OSD, elem_name);
	if(!osd_bin->nvosd)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	g_object_set(G_OBJECT(osd_bin->nvosd), "display-clock", config->enable_clock, "clock-font", config->font.c_str(),
							 "x-clock-offset", config->clock_x_offset, "y-clock-offset", config->clock_y_offset, "clock-color",
							 clk_color, "clock-font-size", config->clock_text_size, "process-mode", config->mode, nullptr);

	gst_bin_add_many(GST_BIN(osd_bin->bin), osd_bin->queue, osd_bin->nvvidconv, osd_bin->conv_queue, osd_bin->nvosd, nullptr);

	g_object_set(G_OBJECT(osd_bin->nvvidconv), "gpu-id", config->gpu_id, nullptr);

	g_object_set(G_OBJECT(osd_bin->nvvidconv), "nvbuf-memory-type", config->nvbuf_memory_type, nullptr);

	g_object_set(G_OBJECT(osd_bin->nvosd), "gpu-id", config->gpu_id, nullptr);
	g_object_set(G_OBJECT(osd_bin->nvosd), "display-text", config->display_text, nullptr);
	g_object_set(G_OBJECT(osd_bin->nvosd), "display-bbox", config->display_bbox, nullptr);
	g_object_set(G_OBJECT(osd_bin->nvosd), "display-mask", config->display_mask, nullptr);
	if(config->mode == NvOSD_Mode::MODE_NONE && !config->hw_blend_color_attr.empty())
		g_object_set(G_OBJECT(osd_bin->nvosd), "hw-blend-color-attr", config->hw_blend_color_attr.c_str(), nullptr);

	TADS_LINK_ELEMENT(osd_bin->queue, osd_bin->nvvidconv);

	TADS_LINK_ELEMENT(osd_bin->nvvidconv, osd_bin->conv_queue);

	TADS_LINK_ELEMENT(osd_bin->conv_queue, osd_bin->nvosd);

	TADS_BIN_ADD_GHOST_PAD(osd_bin->bin, osd_bin->queue, "sink");

	TADS_BIN_ADD_GHOST_PAD(osd_bin->bin, osd_bin->nvosd, "src");

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}
