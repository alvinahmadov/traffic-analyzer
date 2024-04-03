#ifndef TADS_OSD_HPP
#define TADS_OSD_HPP

#include <nvll_osd_struct.h>

#include "common.hpp"

struct OSDConfig
{
	/**
	 * Enables or disables the On-Screen Display (OSD).
	 *
	 * \example enable=1
	 * */
	bool enable{};
	/**
	 * GPU to be used by the element in case of multiple
	 * GPUs.
	 * */
	uint gpu_id{};
	/**
	 * Border width of the bounding boxes drawn for
	 * objects, in pixels.
	 * */
	int border_width{ 1 };
	/**
	 * Border color of the bounding boxes drawn for objects.
	 *
	 * \example
	 * border-color=0;0;0.7;1 #Dark Blue
	 * */
	std::string border_color{ "0;1;0;1" };
	bool text_has_bg;
	bool enable_clock;
	/**
	 * Indicate whether to display text.
	 * */
	bool display_text{ true };
	/**
	 * Indicate whether to bounding box.
	 * */
	bool display_bbox{ true };
	/**
	 * Indicate whether to display instance mask.
	 * */
	bool display_mask{ false };
	/**
	 * Size of the text that describes the objects, in points.
	 * */
	int text_size{ 14 };
	/**
	 * The size of the clock time text, in points.
	 * */
	int clock_text_size{ 14 };
	/**
	 * The horizontal offset of the clock time text, in pixels.
	 * */
	int clock_x_offset{};
	/**
	 * The vertical offset of the clock time text, in pixels.
	 * */
	int clock_y_offset{};
	/**
	 * Type of CUDA memory the element is to allocate
	 * for output buffers.
	 * */
	NvBufMemoryType nvbuf_memory_type{ NvBufMemoryType::DEFAULT };
	uint num_out_buffers{ 8 };
	/**
	 * Name of the font for text that describes the objects.
	 * */
	std::string font{ "14" };
	std::string hw_blend_color_attr;
	NvOSD_Mode mode{ NvOSD_Mode ::MODE_GPU };
	/**
	 * Color of the clock time text, in RGBA format.
	 * */
	NvOSD_ColorParams clock_color{ 0.0, 1.0, 0.0, 0.0 };
	NvOSD_ColorParams text_color;
	/**
	 * The background color of the text that describes the objects, in RGBA format.
	 * */
	NvOSD_ColorParams text_bg_color{ 0.1, 0.1, 0.1, 1.0 };
};

struct OSDBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *nvvidconv;
	GstElement *conv_queue;
	[[maybe_unused]] GstElement *cap_filter;
	GstElement *nvosd;
};

/**
 * Initialize @ref NvDsOSDBin. It creates and adds OSD and other elements
 * needed for processing to the bin. It also sets properties mentioned
 * in the configuration file under group @ref CONFIG_GROUP_OSD
 *
 * @param[in] config pointer to OSD @ref NvDsOSDConfig parsed from config file.
 * @param[in] osd_bin pointer to @ref NvDsOSDBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_osd_bin(OSDConfig *config, OSDBin *osd_bin);

#endif // TADS_OSD_HPP
