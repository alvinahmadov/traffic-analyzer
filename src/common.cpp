#include <cassert>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include "common.hpp"

namespace gst
{

GstElement *bin_new(std::string_view name)
{
	return gst_bin_new(name.data());
}

GstElement *element_factory_make(std::string_view factoryname)
{
	return gst_element_factory_make(factoryname.data(), nullptr);
}

GstElement *element_factory_make(std::string_view factoryname, std::string_view name)
{
	return gst_element_factory_make(factoryname.data(), name.empty() ? nullptr : name.data());
}

GstPad *element_request_pad_simple(GstElement *element, std::string_view name)
{
	const char *pad_name = nullptr;
	if(!name.empty())
	{
		pad_name = name.data();
	}
#if NVDS_VERSION_MINOR >= 4
	return gst_element_request_pad_simple(element, pad_name);
#else
	return gst_element_get_request_pad(element, pad_name);
#endif
}

bool link_element_to_tee_src_pad(GstElement *tee, GstElement *element)
{
	bool success{};
	GstPad *tee_src_pad{};
	GstPad *sinkpad{};
	GstPadTemplate *padtemplate;

	padtemplate = static_cast<GstPadTemplate *>(gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(tee), "src_%u"));
	tee_src_pad = gst_element_request_pad(tee, padtemplate, nullptr, nullptr);
	if(!tee_src_pad)
	{
		TADS_ERR_MSG_V("Failed to get src pad from tee");
		goto done;
	}

	sinkpad = gst_element_get_static_pad(element, "sink");
	if(!sinkpad)
	{
		TADS_ERR_MSG_V("Failed to get sink pad from '%s'", GST_ELEMENT_NAME(element));
		goto done;
	}

	if(gst_pad_link(tee_src_pad, sinkpad) != GST_PAD_LINK_OK)
	{
		TADS_ERR_MSG_V("Failed to link '%s' and '%s'", GST_ELEMENT_NAME(tee), GST_ELEMENT_NAME(element));
		goto done;
	}

	success = true;

done:
	if(tee_src_pad)
	{
		gst_object_unref(tee_src_pad);
	}
	if(sinkpad)
	{
		gst_object_unref(sinkpad);
	}
	return success;
}

bool link_element_to_streammux_sink_pad(GstElement *streammux, GstElement *elem, int index)
{
	bool success{};
	GstPad *mux_sink_pad{};
	GstPad *src_pad{};
	std::string pad_name;

	if(index >= 0)
	{
		pad_name = fmt::format("sink_{}", index);
	}
	else
	{
		pad_name = "sink_%u";
	}

	mux_sink_pad = gst::element_request_pad_simple(streammux, pad_name);

	if(!mux_sink_pad)
	{
		TADS_ERR_MSG_V("Failed to get sink pad from streammux");
		goto done;
	}

	src_pad = gst_element_get_static_pad(elem, "src");
	if(!src_pad)
	{
		TADS_ERR_MSG_V("Failed to get src pad from '%s'", GST_ELEMENT_NAME(elem));
		goto done;
	}

	if(gst_pad_link(src_pad, mux_sink_pad) != GST_PAD_LINK_OK)
	{
		TADS_ERR_MSG_V("Failed to link '%s' and '%s'", GST_ELEMENT_NAME(streammux), GST_ELEMENT_NAME(elem));
		goto done;
	}

	success = true;

done:
	if(mux_sink_pad)
	{
		gst_object_unref(mux_sink_pad);
	}
	if(src_pad)
	{
		gst_object_unref(src_pad);
	}
	return success;
}

[[maybe_unused]]
bool unlink_element_from_streammux_sink_pad(GstElement *streammux, GstElement *elem)
{
	bool success{};
	GstPad *mux_sink_pad{};
	GstPad *src_pad;

	src_pad = gst_element_get_static_pad(elem, "src");
	if(!src_pad)
	{
		TADS_ERR_MSG_V("Failed to get src pad from '%s'", GST_ELEMENT_NAME(elem));
		goto done;
	}

	mux_sink_pad = gst_pad_get_peer(src_pad);
	if(!mux_sink_pad)
	{
		TADS_ERR_MSG_V("Failed to get sink pad from streammux");
		goto done;
	}

	if(!gst_pad_unlink(src_pad, mux_sink_pad))
	{
		TADS_ERR_MSG_V("Failed to unlink '%s' and '%s'", GST_ELEMENT_NAME(streammux), GST_ELEMENT_NAME(elem));
		goto done;
	}

	gst_element_release_request_pad(streammux, mux_sink_pad);

	success = true;

done:
	if(mux_sink_pad)
	{
		gst_object_unref(mux_sink_pad);
	}
	if(src_pad)
	{
		gst_object_unref(src_pad);
	}
	return success;
}

[[maybe_unused]]
bool link_element_to_demux_src_pad(GstElement *demux, GstElement *elem, uint index)
{
	bool success{};
	GstPad *demux_src_pad;
	GstPad *sink_pad{};
	std::string pad_name;

	pad_name = fmt::format("src_{}", index);
	demux_src_pad = gst::element_request_pad_simple(demux, pad_name);

	if(!demux_src_pad)
	{
		TADS_ERR_MSG_V("Failed to get sink pad from demux");
		goto done;
	}

	sink_pad = gst_element_get_static_pad(elem, "sink");
	if(!sink_pad)
	{
		TADS_ERR_MSG_V("Failed to get src pad from '%s'", GST_ELEMENT_NAME(elem));
		goto done;
	}

	if(gst_pad_link(demux_src_pad, sink_pad) != GST_PAD_LINK_OK)
	{
		TADS_ERR_MSG_V("Failed to link '%s' and '%s'", GST_ELEMENT_NAME(demux), GST_ELEMENT_NAME(elem));
		goto done;
	}

	success = true;

done:
	if(demux_src_pad)
	{
		gst_object_unref(demux_src_pad);
	}
	if(sink_pad)
	{
		gst_object_unref(sink_pad);
	}
	return success;
}
} // namespace gst

bool starts_with(std::string_view view, std::string_view prefix) noexcept
{
	return view.substr(0, prefix.length()) == prefix;
}

bool starts_with(std::string_view view, std::string_view prefix, size_t length) noexcept
{
	return view.substr(0, length - 1) == prefix;
}

bool ends_with(std::string_view view, std::string_view suffix) noexcept
{
	const size_t view_len = view.length();
	const size_t suff_len = suffix.length();
	return view_len >= suff_len && view.compare(view_len - suff_len, suff_len, suffix) == 0;
}

std::string_view get_suffix(std::string_view view, std::string_view prefix) noexcept
{
	const size_t view_len = view.length();
	const size_t pref_len = prefix.length();

	if(view_len > pref_len)
		return view.substr(view_len - 1, view_len - pref_len);

	return {};
}

std::string join(std::vector<std::string> list, const char *sep)
{
	const size_t size{ list.size() };
	std::string joined_text{};

	for(size_t k{}; k < size; ++k)
	{
		joined_text.append(list.at(k));
		if(k < size - 1)
			joined_text.append(sep);
	}

	return joined_text;
}

static void leftTrim(std::string &s)
{
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
}

static void rightTrim(std::string &s)
{
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(), s.end());
}

std::string trim(std::string &s)
{
	leftTrim(s);
	rightTrim(s);
	return s;
}

std::string to_lower(std::string text)
{
	for(char &c : text)
	{
		c = static_cast<char>(std::tolower(c));
	}
	return text;
}

std::string to_upper(std::string text)
{
	for(char &c : text)
	{
		c = static_cast<char>(std::toupper(c));
	}
	return text;
}

std::vector<std::string> split(const std::string &str, char sep)
{
	std::vector<std::string> chunks;
	size_t pos, prev_pos{};

	do
	{
		pos = str.find_first_of(sep, prev_pos);

		if(pos == std::string::npos)
		{
			break;
		}
		std::string chunk{ str.substr(prev_pos, pos - prev_pos) };
		if(chunk.empty())
		{
			prev_pos = pos + 1;
			continue;
		}
		chunks.push_back(trim(chunk));
		prev_pos = pos + 1;
	}
	while(true);

	return chunks;
}

NvOSD_ColorParams osd_color(const std::string &color_txt)
{
	char sep = ';';
	size_t pos, prevPos{};
	size_t color_idx{};
	double r{}, g{}, b{}, a{ 1 };

	do
	{
		pos = color_txt.find_first_of(sep, prevPos);
		if(pos == std::string::npos)
			break;
		std::string color{ color_txt.substr(prevPos, pos - 1) };
		switch(color_idx)
		{
			case 0:
				r = std::strtod(color.c_str(), nullptr);
				break;
			case 1:
				g = std::strtod(color.c_str(), nullptr);
				break;
			case 2:
				b = std::strtod(color.c_str(), nullptr);
				break;
			case 3:
				a = std::strtod(color.c_str(), nullptr);
				break;
			default:
				break;
		}
		prevPos = pos + 1;
		color_idx++;
	}
	while(color_idx < 4);

	return { r, g, b, a };
}

std::string to_cyrillic(const std::string &text)
{
	std::string localized;

	for(char c : text)
	{
		char c1 = std::toupper(c);
		if(auto at = CHAR_DICT_MAP.find(std::toupper(c1)); at != CHAR_DICT_MAP.end())
			localized.append(at->second);
		else
			localized += std::toupper(c1);
	}

	return localized;
}

#if NVDS_VERSION_MINOR >= 4
std::string get_metatype_name(NvDsMetaType meta_type)
{
	switch(meta_type)
	{
		case NVDS_INVALID_META:
			return "Invalid meta";
		case NVDS_BATCH_META:
			return "NVDS_BATCH_META";
		case NVDS_FRAME_META:
			return "NVDS_FRAME_META";
		case NVDS_OBJ_META:
			return "NVDS_OBJ_META";
		case NVDS_DISPLAY_META:
			return "NVDS_DISPLAY_META";
		case NVDS_CLASSIFIER_META:
			return "NVDS_CLASSIFIER_META";
		case NVDS_LABEL_INFO_META:
			return "NVDS_LABEL_INFO_META";
		case NVDS_USER_META:
			return "NVDS_USER_META";
		case NVDS_PAYLOAD_META:
			return "NVDS_PAYLOAD_META";
		case NVDS_EVENT_MSG_META:
			return "NVDS_EVENT_MSG_META";
		case NVDS_OPTICAL_FLOW_META:
			return "NVDS_OPTICAL_FLOW_META";
		case NVDS_LATENCY_MEASUREMENT_META:
			return "NVDS_LATENCY_MEASUREMENT_META";
		case NVDSINFER_TENSOR_OUTPUT_META:
			return "NVDSINFER_TENSOR_OUTPUT_META";
		case NVDSINFER_SEGMENTATION_META:
			return "NVDSINFER_SEGMENTATION_META";
		case NVDS_CROP_IMAGE_META:
			return "NVDS_CROP_IMAGE_META";
		case NVDS_TRACKER_PAST_FRAME_META:
			return "NVDS_TRACKER_PAST_FRAME_META";
		case NVDS_TRACKER_BATCH_REID_META:
			return "NVDS_TRACKER_BATCH_REID_META";
		case NVDS_TRACKER_OBJ_REID_META:
			return "NVDS_TRACKER_OBJ_REID_META";
		case NVDS_TRACKER_TERMINATED_LIST_META:
			return "NVDS_TRACKER_TERMINATED_LIST_META";
		case NVDS_TRACKER_SHADOW_LIST_META:
			return "NVDS_TRACKER_SHADOW_LIST_META";
		case NVDS_OBJ_VISIBILITY:
			return "NVDS_OBJ_VISIBILITY";
		case NVDS_OBJ_IMAGE_FOOT_LOCATION:
			return "NVDS_OBJ_IMAGE_FOOT_LOCATION";
		case NVDS_OBJ_WORLD_FOOT_LOCATION:
			return "NVDS_OBJ_WORLD_FOOT_LOCATION";
		case NVDS_OBJ_IMAGE_CONVEX_HULL:
			return "NVDS_OBJ_IMAGE_CONVEX_HULL";
		case NVDS_AUDIO_BATCH_META:
			return "NVDS_AUDIO_BATCH_META";
		case NVDS_AUDIO_FRAME_META:
			return "NVDS_AUDIO_FRAME_META";
		case NVDS_PREPROCESS_FRAME_META:
			return "NVDS_PREPROCESS_FRAME_META";
		case NVDS_PREPROCESS_BATCH_META:
			return "NVDS_PREPROCESS_BATCH_META";
		case NVDS_CUSTOM_MSG_BLOB:
			return "NVDS_CUSTOM_MSG_BLOB";
		default:
			return "NVDS_RESERVED_META";
	}
}
#endif