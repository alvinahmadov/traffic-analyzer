#include <iomanip>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <unordered_map>

#include "common.hpp"

static const std::unordered_map<char, std::string> CHAR_DICT_MAP{
	{ 'B', "8" },
	{ 'C', "С" },
	{ 'E', "Е" },
	{ 'H', "Н" },
	{ 'K', "К" },
	{ 'G', "О" },
	{ 'J', "1" },
	{ 'O', "О" },
	{ 'Q', "O" },
	{ 'S', "5" },
	{ 'V', "У" },
	{ 'X', "Х" },
	{ 'Y', "У" },
	{ 'Z', "2" },
};

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
	GstPad *tee_src_pad;
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
	GstPad *mux_sink_pad;
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

std::string get_current_date_time_str(std::string_view format)
{
	if(format.empty())
		return "";

	auto time_point = std::chrono::system_clock::now();
	std::time_t in_time_t = std::chrono::system_clock::to_time_t(time_point);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&in_time_t), format.data());
	auto duration =
			std::chrono::duration_cast<std::chrono::microseconds>(time_point.time_since_epoch()) % std::chrono::seconds{ 1 };
	ss << fmt::format(".{:06}", duration.count());
	return ss.str();
}

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

[[maybe_unused]]
std::string to_lower(std::string text)
{
	for(char &c : text)
	{
		c = static_cast<char>(std::tolower(c));
	}
	return text;
}

[[maybe_unused]]
std::string to_upper(std::string text)
{
	for(char &c : text)
	{
		c = static_cast<char>(std::toupper(c));
	}
	return text;
}

[[maybe_unused]]
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

std::vector<std::string> split(const std::string &str, const std::string &sep)
{
	std::vector<std::string> chunks;
	auto index = str.find(sep);

	if(index != std::string::npos && index > 0)
	{
		chunks.emplace_back(str.substr(0, index - 1));
		if(index + sep.length() < str.length())
			chunks.emplace_back(str.substr(index + sep.length()));
	}
	return chunks;
}

NvOSD_ColorParams osd_color(const std::string &color_txt)
{
	char sep = ';';
	size_t pos, prev_pos{};
	size_t color_idx{};
	std::array<double, 4> colors{ 0.0, 0.0, 0.0, 0.0 };

	do
	{
		pos = color_txt.find_first_of(sep, prev_pos);
		if(pos == std::string::npos)
			break;
		std::string color{ color_txt.substr(prev_pos, pos - 1) };
		colors.at(color_idx++) = std::strtod(color.c_str(), nullptr);
		prev_pos = pos + 1;
	}
	while(color_idx < 4);

	return { colors[0], colors[1], colors[2], colors[3] };
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