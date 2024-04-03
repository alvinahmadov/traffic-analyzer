#ifndef TADS_COMMON_HPP
#define TADS_COMMON_HPP

#include <vector>
#include <memory>
#include <unordered_map>

#include <gst/gst.h>
#include <fmt/format.h>
#include <nvll_osd_api.h>
#include <nvdsmeta.h>

#include "config.hpp"

#define TADS_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifndef TADS_ERR_MSG_V
#define TADS_ERR_MSG_V(msg, ...) printf("ERROR: %s:%d:%s " msg "\n", TADS_FILENAME, __LINE__, __func__, ##__VA_ARGS__)
#endif

#ifndef TADS_INFO_MSG_V
#define TADS_INFO_MSG_V(msg, ...) printf("INFO: %s:%d:%s " msg "\n", TADS_FILENAME, __LINE__, __func__, ##__VA_ARGS__)
#endif

#ifndef TADS_DBG_MSG_V
#define TADS_DBG_MSG_V(msg, ...) printf("DEBUG: %s:%d:%s " msg "\n", TADS_FILENAME, __LINE__, __func__, ##__VA_ARGS__)
#endif

#ifndef TADS_WARN_MSG_V
#define TADS_WARN_MSG_V(msg, ...) printf("WARN: %s:%d:%s " msg "\n", TADS_FILENAME, __LINE__, __func__, ##__VA_ARGS__)
#endif

#ifndef TADS_LINK_ELEMENT
#define TADS_LINK_ELEMENT(elem1, elem2)                                                                               \
	do                                                                                                                  \
	{                                                                                                                   \
		if(!gst_element_link(elem1, elem2))                                                                               \
		{                                                                                                                 \
			GstCaps *src_caps, *sink_caps;                                                                                  \
			src_caps = gst_pad_query_caps((GstPad *)(elem1)->srcpads->data, nullptr);                                       \
			sink_caps = gst_pad_query_caps((GstPad *)(elem2)->sinkpads->data, nullptr);                                     \
			TADS_ERR_MSG_V("Failed to link '%s' (%s) and '%s' (%s)", GST_ELEMENT_NAME(elem1), gst_caps_to_string(src_caps), \
										 GST_ELEMENT_NAME(elem2), gst_caps_to_string(sink_caps));                                         \
			goto done;                                                                                                      \
		}                                                                                                                 \
	}                                                                                                                   \
	while(0)
#endif

#ifndef TADS_LINK_ELEMENT_FULL
#define TADS_LINK_ELEMENT_FULL(elem1, elem1_pad_name, elem2, elem2_pad_name) \
	do                                                                         \
	{                                                                          \
		GstPad *elem1_pad = gst_element_get_static_pad(elem1, elem1_pad_name);   \
		GstPad *elem2_pad = gst_element_get_static_pad(elem2, elem2_pad_name);   \
		GstPadLinkReturn ret = gst_pad_link(elem1_pad, elem2_pad);               \
		if(ret != GST_PAD_LINK_OK)                                               \
		{                                                                        \
			char *n1 = gst_pad_get_name(elem1_pad);                                \
			char *n2 = gst_pad_get_name(elem2_pad);                                \
			TADS_ERR_MSG_V("Failed to link '%s' and '%s': %d", n1, n2, ret);       \
			g_free(n1);                                                            \
			g_free(n2);                                                            \
			gst_object_unref(elem1_pad);                                           \
			gst_object_unref(elem2_pad);                                           \
			goto done;                                                             \
		}                                                                        \
		gst_object_unref(elem1_pad);                                             \
		gst_object_unref(elem2_pad);                                             \
	}                                                                          \
	while(0)
#endif

#ifndef TADS_BIN_ADD_GHOST_PAD_NAMED
#define TADS_BIN_ADD_GHOST_PAD_NAMED(bin, elem, pad, ghost_pad_name)              \
	do                                                                              \
	{                                                                               \
		GstPad *gstpad = gst_element_get_static_pad(elem, pad);                       \
		if(!gstpad)                                                                   \
		{                                                                             \
			TADS_ERR_MSG_V("Could not find '%s' in '%s'", pad, GST_ELEMENT_NAME(elem)); \
			goto done;                                                                  \
		}                                                                             \
		gst_element_add_pad(bin, gst_ghost_pad_new(ghost_pad_name, gstpad));          \
		gst_object_unref(gstpad);                                                     \
	}                                                                               \
	while(0)
#endif

#ifndef TADS_BIN_ADD_GHOST_PAD
#define TADS_BIN_ADD_GHOST_PAD(bin, elem, pad) TADS_BIN_ADD_GHOST_PAD_NAMED(bin, elem, pad, pad)
#endif

#ifndef TADS_ELEM_ADD_PROBE
#define TADS_ELEM_ADD_PROBE(probe_id, elem, pad, probe_func, probe_type, probe_data)     \
	do                                                                                     \
	{                                                                                      \
		GstPad *gstpad = gst_element_get_static_pad(elem, pad);                              \
		if(!gstpad)                                                                          \
		{                                                                                    \
			TADS_ERR_MSG_V("Could not find '%s' in '%s'", pad, GST_ELEMENT_NAME(elem));        \
			goto done;                                                                         \
		}                                                                                    \
		probe_id = gst_pad_add_probe(gstpad, (probe_type), probe_func, probe_data, nullptr); \
		gst_object_unref(gstpad);                                                            \
	}                                                                                      \
	while(0)
#endif

#ifndef TADS_ELEM_REMOVE_PROBE
#define TADS_ELEM_REMOVE_PROBE(probe_id, elem, pad)                               \
	do                                                                              \
	{                                                                               \
		if(probe_id == 0 || !elem)                                                    \
		{                                                                             \
			break;                                                                      \
		}                                                                             \
		GstPad *gstpad = gst_element_get_static_pad(elem, pad);                       \
		if(!gstpad)                                                                   \
		{                                                                             \
			TADS_ERR_MSG_V("Could not find '%s' in '%s'", pad, GST_ELEMENT_NAME(elem)); \
			break;                                                                      \
		}                                                                             \
		gst_pad_remove_probe(gstpad, probe_id);                                       \
		gst_object_unref(gstpad);                                                     \
	}                                                                               \
	while(0)
#endif

#ifndef TADS_CHECK_MEMORY_AND_GPUID
#define TADS_CHECK_MEMORY_AND_GPUID(object, surface)                                                       \
	({                                                                                                       \
		do                                                                                                     \
		{                                                                                                      \
			if((surface->memType == NVBUF_MEM_DEFAULT || surface->memType == NVBUF_MEM_CUDA_DEVICE) &&           \
				 (surface->gpuId != object->gpu_id))                                                               \
			{                                                                                                    \
				GST_ELEMENT_ERROR(                                                                                 \
						object, RESOURCE, FAILED,                                                                      \
						("Input surface gpu-id doesnt match with configured gpu-id for element,"                       \
						 " please allocate input using unified memory, or use same gpu-ids"),                          \
						("surface-gpu-id=%d,%s-gpu-id=%d", surface->gpuId, GST_ELEMENT_NAME(object), object->gpu_id)); \
				goto done;                                                                                         \
			}                                                                                                    \
		}                                                                                                      \
		while(0);                                                                                              \
	})
#endif

#ifndef TADS_GET_FILE_PATH
#define TADS_GET_FILE_PATH(path) ((path) + (((path) && strstr((path), "file://")) ? 7 : 0))
#endif

const std::unordered_map<char, std::string> CHAR_DICT_MAP{
	{ 'B', "Б" },
	{ 'C', "С" },
	{ 'E', "Е" },
	{ 'H', "Н" },
	{ 'K', "К" },
	{ 'G', "О" },
	{ 'J', "1" },
	{ 'O', "О" },
	{ 'Q', "O" },
	{ 'S', "5" },
	{ 'Y', "У" },
	{ 'V', "У" },
	{ 'W', "Ш" },
	{ 'Y', "У" },
	{ 'V', "У" },
	{ 'Z', "2"	 },
};

enum class NvBufMemoryType : uint
{
	DEFAULT = 0,									 ///< platform-specific default
	CUDA_PINNED [[maybe_unused]],	 ///< pinned/host CUDA memory
	CUDA_DEVICE [[maybe_unused]],	 ///< Device CUDA memory
	CUDA_UNIFIED [[maybe_unused]], ///< Unified CUDA memory
	SURFACE_ARRAY [[maybe_unused]],
};

namespace gst
{
GstElement *bin_new(std::string_view name);
GstElement *element_factory_make(std::string_view factoryname);
GstElement *element_factory_make(std::string_view factoryname, std::string_view name);
GstPad *element_request_pad_simple(GstElement *element, std::string_view name = "");

/**
 * Function to link sink pad of an element to source pad of tee.
 *
 * @param[in] tee Tee element.
 * @param[in] element downstream element.
 *
 * @return true if link successful.
 */
bool link_element_to_tee_src_pad(GstElement *tee, GstElement *element);

/**
 * Function to link source pad of an element to sink pad of muxer element.
 *
 * @param[in] streammux muxer element.
 * @param[in] elem upstream element.
 * @param[in] index pad instance_num of muxer element.
 *
 * @return true if link successful.
 */
bool link_element_to_streammux_sink_pad(GstElement *streammux, GstElement *elem, int index);

/**
 * Function to unlink source pad of an element from sink pad of muxer element.
 *
 * @param[in] streammux muxer element.
 * @param[in] elem upstream element.
 *
 * @return true if unlinking was successful.
 */
[[maybe_unused]]
bool unlink_element_from_streammux_sink_pad(GstElement *streammux, GstElement *elem);

/**
 * Function to link sink pad of an element to source pad of demux element.
 *
 * @param[in] demux demuxer element.
 * @param[in] elem downstream element.
 * @param[in] index pad instance_num of demuxer element.
 *
 * @return true if link successful.
 */
[[maybe_unused]]
bool link_element_to_demux_src_pad(GstElement *demux, GstElement *elem, uint index);
} // namespace gst

bool starts_with(std::string_view view, std::string_view prefix) noexcept;
bool starts_with(std::string_view view, std::string_view prefix, size_t length) noexcept;
bool ends_with(std::string_view view, std::string_view suffix) noexcept;
std::string_view get_suffix(std::string_view view, std::string_view prefix) noexcept;

std::string join(std::vector<std::string> list, const char *sep);

std::string trim(std::string &s);

[[maybe_unused]]
std::string to_lower(std::string text);

[[maybe_unused]]
std::string to_upper(std::string text);

[[maybe_unused]]
std::vector<std::string> split(const std::string &str, char sep = ' ');
std::vector<std::string> split(const std::string &str, const std::string &sep);

NvOSD_ColorParams osd_color(const std::string &color_txt);

std::string to_cyrillic(const std::string &text);

struct BaseConfig
{
	BaseConfig() = default;
	virtual ~BaseConfig() = default;

	BaseConfig(const BaseConfig &) = default;
	BaseConfig &operator=(const BaseConfig &) = default;

	[[maybe_unused]] [[nodiscard]]
	inline bool isParsed() const
	{
		return this->is_parsed;
	}

protected:
	bool is_parsed{};
};

struct BaseBin
{
	BaseBin() = default;
	virtual ~BaseBin() = default;

	[[maybe_unused]] [[nodiscard]]
	inline bool isInit() const
	{
		return this->is_init;
	}

protected:
	bool is_init{};
};

#endif // TADS_COMMON_HPP
