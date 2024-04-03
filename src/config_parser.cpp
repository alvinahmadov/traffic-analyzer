#include <cstring>
#include <unistd.h>

#include <fstream>
#include <filesystem>

#include "app.hpp"
#include "config_parser.hpp"

const size_t G_PATH_MAX{ 1024 };

GST_DEBUG_CATEGORY(APP_CFG_PARSER_CAT);

#define CHECK_ERROR(error)                                   \
	if(error)                                                  \
	{                                                          \
		GST_CAT_ERROR(APP_CFG_PARSER_CAT, "%s", error->message); \
		goto done;                                               \
	}

namespace glib
{
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
static bool key_file_load_from_file(GKeyFile *key_file, std::string_view file, GKeyFileFlags flags, GError **error)
{
	return g_key_file_load_from_file(key_file, file.data(), flags, error);
}
#pragma clang diagnostic pop

static std::vector<std::string> key_file_get_groups(GKeyFile *key_file, gsize *length = nullptr)
{
	std::vector<std::string> string_list{};
	auto keys = g_key_file_get_groups(key_file, length);
	for(auto iter = keys; *iter != nullptr; ++iter)
		string_list.emplace_back(*iter);

	g_strfreev(keys);
	return string_list;
}

static std::vector<std::string>
key_file_get_keys(GKeyFile *key_file, std::string_view group_name, gsize *length, GError **error)
{
	std::vector<std::string> string_list{};
	auto keys = g_key_file_get_keys(key_file, group_name.data(), length, error);
	for(auto iter = keys; *iter != nullptr; ++iter)
		string_list.emplace_back(*iter);

	g_strfreev(keys);
	return string_list;
}

static bool key_file_has_key(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_has_key(key_file, group_name.data(), key.data(), error);
}

static bool key_file_has_group(GKeyFile *key_file, std::string_view group_name)
{
	return g_key_file_has_group(key_file, group_name.data());
}

static bool key_file_remove_group(GKeyFile *key_file, std::string_view group_name, GError **error)
{
	return g_key_file_remove_group(key_file, group_name.data(), error);
}

static bool key_file_get_boolean(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_get_boolean(key_file, group_name.data(), key.data(), error);
}

static int key_file_get_integer(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_get_integer(key_file, group_name.data(), key.data(), error);
}

static uint64_t
key_file_get_uint64(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_get_uint64(key_file, group_name.data(), key.data(), error);
}

static std::vector<int> key_file_get_integer_list(GKeyFile *key_file, std::string_view group_name, std::string_view key,
																									gsize *length = nullptr, GError **error = nullptr)
{
	std::vector<int> int_list;

	auto keys = g_key_file_get_integer_list(key_file, group_name.data(), key.data(), length, error);

	for(int i{}; i < *length; ++i)
	{
		int_list.emplace_back(keys[i]);
	}

	return int_list;
}

static double key_file_get_double(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_get_double(key_file, group_name.data(), key.data(), error);
}

static double *key_file_get_double_list(GKeyFile *key_file, std::string_view group_name, std::string_view key,
																				gsize *length, GError **error)
{
	return g_key_file_get_double_list(key_file, group_name.data(), key.data(), length, error);
}

static std::string
key_file_get_string(GKeyFile *key_file, std::string_view group_name, std::string_view key, GError **error)
{
	return g_key_file_get_string(key_file, group_name.data(), key.data(), error);
}

static std::vector<std::string> key_file_get_string_list(GKeyFile *key_file, std::string_view group_name,
																												 std::string_view key, gsize *length, GError **error)
{
	std::vector<std::string> string_list{};

	auto keys = g_key_file_get_string_list(key_file, group_name.data(), key.data(), length, error);

	for(auto iter = keys; iter != nullptr; ++iter)
	{
		string_list.emplace_back(*iter);
	}

	return string_list;
}
} // namespace glib

static std::vector<std::string> split_csv_entries(std::string input)
{
	std::vector<int> positions;
	for(uint i{}; i < input.size(); i++)
	{
		if(input[i] == ',')
			positions.push_back(i);
	}
	std::vector<std::string> ret;
	int prev = 0;
	for(auto &j : positions)
	{
		std::string temp = input.substr(prev, j - prev);
		ret.push_back(temp);
		prev = j + 1;
	}
	ret.push_back(input.substr(prev, input.size() - prev));
	return ret;
}

static std::vector<std::string> split_string(std::string input)
{
	std::vector<int> positions;
	for(unsigned int i = 0; i < input.size(); i++)
	{
		if(input[i] == ';')
			positions.push_back(i);
	}
	std::vector<std::string> chunks;
	int prev{};
	for(auto &j : positions)
	{
		auto temp = input.substr(prev, j - prev);
		chunks.push_back(temp);
		prev = j + 1;
	}
	chunks.push_back(input.substr(prev, input.size() - prev));
	return chunks;
}

/**
 * Utility function to convert relative path in configuration file
 * with absolute path.
 *
 * @param[in] cfg_file_path path of configuration file.
 * @param[in] file_path relative path of file.
 */
static std::string get_absolute_file_path(std::string_view cfg_file_path, const std::string &file_path)
{
	char abs_cfg_path[PATH_MAX + 1];
	std::string abs_file_path;
	char *delim;

	if(!file_path.empty() && file_path.at(0) == '/')
	{
		return file_path;
	}

	if(!realpath(cfg_file_path.data(), abs_cfg_path))
	{
		return {};
	}
	// Return absolute path of config file if img_filename is nullptr.
	if(file_path.empty())
	{
		abs_file_path = g_strdup(abs_cfg_path);
		return abs_file_path;
	}

	delim = g_strrstr(abs_cfg_path, "/");
	*(delim + 1) = '\0';

	abs_file_path = abs_cfg_path + file_path;
	return abs_file_path;
}

/**
 * Get the absolute path of a file mentioned in the config given a
 * file path absolute/relative to the config file.
 *
 * @param cfg_file_path [in] Path to the configuration file.
 * @param file_path [in] Relative path to the file.
 * @param abs_path_str [out] Absolute path to the file.
 *
 * @returns true if file exists
 *
 * */
static bool
get_absolute_file_path_yaml(std::string_view cfg_file_path, std::string_view file_path, std::string &abs_path_str)
{
	char abs_cfg_path[PATH_MAX + 1];
	char abs_real_file_path[PATH_MAX + 1];
	char *abs_file_path;
	char *delim;

	/* Absolute path. No need to resolve further. */
	if(file_path[0] == '/')
	{
		/* Check if the file exists, return error if not. */
		if(!realpath(file_path.data(), abs_real_file_path))
		{
			/* Ignore error if file does not exist and use the unresolved path. */
			if(errno != ENOENT)
				return false;
		}
		abs_path_str = abs_real_file_path;
		return true;
	}

	/* Get the absolute path of the config file. */
	if(!realpath(cfg_file_path.data(), abs_cfg_path))
	{
		return false;
	}

	/* Remove the file name from the absolute path to get the directory of the
	 * config file. */
	delim = g_strrstr(abs_cfg_path, "/");
	*(delim + 1) = '\0';

	/* Get the absolute file path from the config file's directory path and
	 * relative file path. */
	abs_file_path = g_strconcat(abs_cfg_path, file_path, nullptr);

	/* Resolve the path.*/
	if(realpath(abs_file_path, abs_real_file_path) == nullptr)
	{
		/* Ignore error if file does not exist and use the unresolved path. */
		if(errno == ENOENT)
			g_strlcpy(abs_real_file_path, abs_file_path, G_PATH_MAX);
		else
			return false;
	}

	g_free(abs_file_path);

	abs_path_str = abs_real_file_path;
	return true;
}

/**
 * Function to parse class label file. Parses the labels into a 2D-array of
 * strings. Refer the SDK documentation for format of the labels file.
 *
 * @param[in] config pointer to @ref NvDsGieConfig
 *
 * @return true if file parsed successfully else returns false.
 */
static bool parse_labels_file(GieConfig *config)
{
	gsize term_pos;
	GIOStatus status;
	uint j;
	GList *labels_iter;
	GIOChannel *label_file;
	GList *label_outputs_length_iter;
	GList *labels_list{};
	GList *label_outputs_length_list{};
	GError *err{};
	bool success{};

	label_file = g_io_channel_new_file(TADS_GET_FILE_PATH(config->label_file_path.c_str()), "r", &err);

	if(err)
	{
		TADS_ERR_MSG_V("Failed to open label file '%s':%s", config->label_file_path.c_str(), err->message);
		goto done;
	}

	/* Iterate over each line */
	do
	{
		char *temp;
		char *line_str = nullptr;
		char *iter;
		GList *label_outputs_list = nullptr;
		uint label_outputs_length = 0;
		GList *list_iter;
		uint i;
		char **label_outputs;

		/* Read the line into 'line_str' char array */
		status = g_io_channel_read_line(label_file, (char **)&line_str, nullptr, &term_pos, &err);

		if(line_str == nullptr)
			continue;

		temp = line_str;

		temp[term_pos] = '\0';

		/* Parse ';' delimited strings and prepend to the 'labels_output_list'.
		 * Prepending to a list and reversing after adding all strings is faster
		 * than appending every string.
		 * https://developer.gnome.org/glib/stable/glib-Doubly-Linked-Lists.html#g-list-append
		 */
		while((iter = g_strstr_len(temp, -1, ";")))
		{
			*iter = '\0';
			label_outputs_list = g_list_prepend(label_outputs_list, g_strdup(temp));
			label_outputs_length++;
			temp = iter + 1;
		}

		if(*temp != '\0')
		{
			label_outputs_list = g_list_prepend(label_outputs_list, g_strdup(temp));
			label_outputs_length++;
		}

		/* All labels in one line parsed and added. Now reverse the list. */
		label_outputs_list = g_list_reverse(label_outputs_list);
		list_iter = label_outputs_list;

		/* Convert the list to an array for faster access. */
		label_outputs = (char **)g_malloc0(sizeof(char *) * label_outputs_length);
		for(i = 0; i < label_outputs_length; i++)
		{
			label_outputs[i] = (char *)list_iter->data;
			list_iter = list_iter->next;
		}
		g_list_free(label_outputs_list);

		/* Prepend the pointer to array of labels in one line to 'labels_list'. */
		labels_list = g_list_prepend(labels_list, label_outputs);
		/* Prepend the corresponding array size to 'label_outputs_length_list'. */
		label_outputs_length_list = g_list_prepend(label_outputs_length_list, (char *)nullptr + label_outputs_length);

		/* Maintain the number of labels(lines). */
		config->n_labels++;
		g_free(line_str);
	}
	while(status == G_IO_STATUS_NORMAL);
	g_io_channel_unref(label_file);
	success = true;

	/* Convert the 'labels_list' and the 'label_outputs_length_list' to arrays
	 * for faster access. */
	config->n_label_outputs = new uint[config->n_labels];
	config->labels = (char ***)g_malloc(config->n_labels * sizeof(char **));

	labels_list = g_list_reverse(labels_list);
	label_outputs_length_list = g_list_reverse(label_outputs_length_list);

	labels_iter = labels_list;
	label_outputs_length_iter = label_outputs_length_list;
	for(j = 0; j < config->n_labels; j++)
	{
		config->labels[j] = (char **)labels_iter->data;
		labels_iter = labels_iter->next;
		config->n_label_outputs[j] = (char *)label_outputs_length_iter->data - (char *)nullptr;
		label_outputs_length_iter = label_outputs_length_iter->next;
	}

	g_list_free(labels_list);
	g_list_free(label_outputs_length_list);

done:
	return success;
}

static bool set_source_all_configs(AppConfig *config, std::string_view cfg_file_path)
{
	SourceConfig *multi_source_config;

	for(uint i{}; i < config->total_num_sources; i++)
	{
		multi_source_config = &config->multi_source_configs.at(i);

		*multi_source_config = config->source_attr_all_config;
		multi_source_config->camera_id = i;
		if(!config->uri_list.empty())
		{
			std::string_view uri{ config->uri_list.at(i) };
			if(uri.empty())
			{
				TADS_ERR_MSG_V("uri %d entry of list is null, use valid uri separated by ';' with the source-list section",
											 (i + 1));
				return false;
			}
			if(starts_with(uri, "file://"))
			{
				const size_t len = strlen("file://");
				multi_source_config->type = SourceType::URI;
				multi_source_config->uri = uri.substr(len);
				multi_source_config->uri =
						fmt::format("file://{}", get_absolute_file_path(cfg_file_path, multi_source_config->uri));
			}
			else if(starts_with(uri, "rtsp://"))
			{
				multi_source_config->type = SourceType::RTSP;
				multi_source_config->uri = uri;
			}
			else
			{
				auto substr = uri.substr(4);
				char *source_id_start_ptr = const_cast<char *>(uri.substr(4).data());
				char *source_id_end_ptr{};
				long camera_id = strtoull(substr.data(), nullptr, 10);
				if(source_id_start_ptr == source_id_end_ptr)
				{
					TADS_ERR_MSG_V("Incorrect URI for camera source %s. FORMAT: <usb/csi>:<dev_node/sensor_id>", uri.data());
					return false;
				}
				if(starts_with(config->uri_list[i], "csi:"))
				{
					multi_source_config->type = SourceType::CAMERA_CSI;
					multi_source_config->camera_csi_sensor_id = camera_id;
				}
				else if(starts_with(config->uri_list[i], "usb:"))
				{
					multi_source_config->type = SourceType::CAMERA_V4L2;
					multi_source_config->camera_v4l2_dev_node = camera_id;
				}
				else
				{
					TADS_ERR_MSG_V("URI %d (%s) not in proper format.", i, config->uri_list[i].c_str());
					return false;
				}
			}
		}
	}
	return true;
}

ConfigParser::ConfigParser(std::string cfg_file_path):
	m_file_type{ ConfigFileType::NONE },
	m_file_path{ std::move(cfg_file_path) }
{
	if(!std::filesystem::exists(std::filesystem::path(m_file_path)))
	{
		TADS_ERR_MSG_V("File does not exist: %s", m_file_path.c_str());
		return;
	}

	if(ends_with(m_file_path, ".yml") || ends_with(m_file_path, ".yaml"))
	{
		m_file_type = ConfigFileType::YAML;
#ifdef TADS_CONFIG_PARSER_DEBUG
		TADS_DBG_MSG_V("Parsing YAML file: %s", m_file_path.c_str());
#endif
	}
	else if(ends_with(m_file_path, ".txt") || ends_with(m_file_path, ".ini"))
	{
		m_file_type = ConfigFileType::INI;
		m_key_file = g_key_file_new();
#ifdef TADS_CONFIG_PARSER_DEBUG
		TADS_DBG_MSG_V("Parsing INI file: %s", m_file_path.c_str());
#endif
	}
	else
	{
		TADS_ERR_MSG_V("File '%s' is not a configuration file.", m_file_path.c_str());
	}
}

ConfigParser::~ConfigParser()
{
	if(m_key_file)
	{
		g_key_file_free(m_key_file);
	}
}

bool ConfigParser::parse(AppConfig *config)
{
	bool is_parsed{};

	switch(m_file_type)
	{
		case ConfigFileType::INI:
			is_parsed = parse_ini(config);
			break;
		case ConfigFileType::YAML:
			is_parsed = parse_yaml(config);
			break;
		default:
			TADS_ERR_MSG_V("File type not recognized");
			return false;
	}

	if(!is_parsed)
	{
		TADS_ERR_MSG_V("Failed to parse config file '%s'", m_file_path.c_str());
	}
	return is_parsed;
}

bool ConfigParser::parse_ini(AppConfig *config)
{
	GError *error{};
	bool success{};
	std::vector<std::string> groups;
	uint i, j;
	uint num_dewarper_source{};

	if(!APP_CFG_PARSER_CAT)
	{
		GST_DEBUG_CATEGORY_INIT(APP_CFG_PARSER_CAT, "NVDS_CFG_PARSER", 0, nullptr);
	}

	if(!glib::key_file_load_from_file(m_key_file, m_file_path, G_KEY_FILE_NONE, &error))
	{
		GST_CAT_ERROR(APP_CFG_PARSER_CAT, "Failed to load uri file: %s", error->message);
		goto done;
	}

	/** App group parsing at top level to set global_gpu_id (if available)
	 * before any other group parsing */
	if(glib::key_file_has_group(m_key_file, CONFIG_GROUP_APP))
	{
		if(!parse_app(config))
		{
			GST_CAT_ERROR(APP_CFG_PARSER_CAT, "Failed to parse '%s' group", CONFIG_GROUP_APP.data());
			goto done;
		}
	}

	if(glib::key_file_has_group(m_key_file, CONFIG_GROUP_SOURCE_ALL))
	{
		/** set gpu_id for source component using global_gpu_id(if available) */
		if(config->global_gpu_id != -1)
		{
			config->source_attr_all_config.gpu_id = config->global_gpu_id;
		}
		/** if gpu_id for source component is present,
		 * it will override the value set using global_gpu_id in parse_source function */
		if(!parse_source(&config->source_attr_all_config, CONFIG_GROUP_SOURCE_ALL))
		{
			GST_CAT_ERROR(APP_CFG_PARSER_CAT, "Failed to parse '%s' group", CONFIG_GROUP_SOURCE_LIST.data());
			goto done;
		}
		config->source_attr_all_parsed = true;
		if(!set_source_all_configs(config, m_file_path))
		{
			success = false;
			goto done;
		}
		glib::key_file_remove_group(m_key_file, CONFIG_GROUP_SOURCE_ALL, &error);
	}

	if(glib::key_file_has_group(m_key_file, CONFIG_GROUP_SOURCE_LIST))
	{
		if(!parse_source_list(config))
		{
			GST_CAT_ERROR(APP_CFG_PARSER_CAT, "Failed to parse '%s' group", CONFIG_GROUP_SOURCE_LIST.data());
			goto done;
		}
		config->num_source_sub_bins = config->total_num_sources;
		config->source_list_enabled = true;
		if(!glib::key_file_has_group(m_key_file, CONFIG_GROUP_SOURCE_ALL))
		{
			TADS_ERR_MSG_V("[source-attr-all] group not present.");
			success = false;
			goto done;
		}
		glib::key_file_remove_group(m_key_file, CONFIG_GROUP_SOURCE_LIST, &error);
	}

	groups = glib::key_file_get_groups(m_key_file);

	for(std::string_view group_name : groups)
	{
		bool parse_err{};
		GST_CAT_DEBUG(APP_CFG_PARSER_CAT, "parsing configuration group: %s", group_name.data());

		if(starts_with(group_name, CONFIG_GROUP_SOURCE))
		{
			if(config->num_source_sub_bins == MAX_SOURCE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
				success = false;
				goto done;
			}
			std::string_view index_str = get_suffix(group_name, CONFIG_GROUP_SOURCE);
			if(index_str.empty())
			{
				TADS_ERR_MSG_V("Source group \"'%s'\" is not in the form \"[source<%%d>]\"", group_name.data());
				success = false;
				goto done;
			}
			uint index = std::strtoull(index_str.data(), nullptr, 10);
			uint source_id;
			if(config->source_list_enabled)
			{
				if(index >= config->total_num_sources)
				{
					TADS_ERR_MSG_V("Invalid source group instance_num %d, instance_num cannot exceed %ld", index,
												 config->total_num_sources);
					success = false;
					goto done;
				}
				source_id = index;
				TADS_INFO_MSG_V("Some parameters to be overwritten for group '%s'", group_name.data());
			}
			else
			{
				source_id = config->num_source_sub_bins;
			}
			/**  set gpu_id for source component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->multi_source_configs[source_id].gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for source component is present,
			 * it will override the value set using global_gpu_id in parse_source function */
			parse_err = !parse_source(&config->multi_source_configs[source_id], group_name);
			if(config->source_list_enabled && config->multi_source_configs[source_id].type == SourceType::URI_MULTIPLE)
			{
				TADS_ERR_MSG_V("MultiURI support not available if [source-list] is provided");
				success = false;
				goto done;
			}
			if(config->multi_source_configs[source_id].enable && !config->source_list_enabled)
			{
				config->num_source_sub_bins++;
			}
		}

		if(group_name == CONFIG_GROUP_PRIMARY_GIE)
		{
			/** set gpu_id for primary gie component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->primary_gie_config.gpu_id = config->global_gpu_id;
				config->primary_gie_config.is_gpu_id_set = true;
			}
			/** if gpu_id for primary gie component is present,
			 * it will override the value set using global_gpu_id in parse_gie function */
			parse_err = !parse_gie(&config->primary_gie_config, CONFIG_GROUP_PRIMARY_GIE);
		}
		else if(starts_with(group_name, CONFIG_GROUP_SECONDARY_GIE))
		{
			if(config->num_secondary_gie_sub_bins == MAX_SECONDARY_GIE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld secondary GIEs", MAX_SECONDARY_GIE_BINS);
				success = false;
				goto done;
			}
			/** set gpu_id for secondary gie component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].gpu_id = config->global_gpu_id;
				config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].is_gpu_id_set = true;
			}
			/**  if gpu_id for secondary gie component is present,
			 * it will override the value set using global_gpu_id in parse_gie function */
			parse_err = !parse_gie(&config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins], group_name);
			if(config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].enable)
			{
				config->num_secondary_gie_sub_bins++;
			}
		}
		else if(starts_with(group_name, CONFIG_GROUP_SECONDARY_PREPROCESS))
		{
			if(config->num_secondary_preprocess_sub_bins == MAX_SECONDARY_PREPROCESS_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld secondary PREPROCESSs", MAX_SECONDARY_PREPROCESS_BINS);
				success = false;
				goto done;
			}
			parse_err = !parse_preprocess(
					&config->secondary_preprocess_sub_bin_configs[config->num_secondary_preprocess_sub_bins], group_name);

			if(config->secondary_preprocess_sub_bin_configs[config->num_secondary_preprocess_sub_bins].enable)
			{
				config->num_secondary_preprocess_sub_bins++;
			}
		}
		else if(group_name == CONFIG_GROUP_STREAMMUX)
		{
			/** set gpu_id for streammux component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->streammux_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for streammux component is present,
			 * it will override the value set using global_gpu_id in parse_streammux function */
			parse_err = !parse_streammux(&config->streammux_config);
		}
		else if(group_name == CONFIG_GROUP_TRACKER)
		{
			/** set gpu_id for tracker component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->tracker_config.gpu_id = config->global_gpu_id;
			}
			/**  if gpu_id for tracker component is present,
			 * it will override the value set using global_gpu_id in parse_tracker function */
			parse_err = !parse_tracker(&config->tracker_config);
		}
		else if(group_name == CONFIG_GROUP_PREPROCESS)
		{
			parse_err = !parse_preprocess(&config->preprocess_config, group_name);
		}
		else if(group_name == CONFIG_GROUP_ANALYTICS)
		{
			parse_err = !parse_analytics(&config->analytics_config);
		}
		else if(group_name == CONFIG_GROUP_OSD)
		{
			/** set gpu_id for osd component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->osd_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for osd component is present,
			 * it will override the value set using global_gpu_id in parse_osd function */
			parse_err = !parse_osd(&config->osd_config);
		}
		else if(starts_with(group_name, CONFIG_GROUP_SINK))
		{
			if(config->num_sink_sub_bins == MAX_SINK_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sinks", MAX_SINK_BINS);
				success = false;
				goto done;
			}
			/** set gpu_id for sink component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				if(glib::key_file_get_integer(m_key_file, group_name, "enable", &error) && error == nullptr)
				{
					config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].encoder_config.gpu_id =
							config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].render_config.gpu_id = config->global_gpu_id;
				}
			}
			/** if gpu_id for sink component is present,
			 * it will override the value set using global_gpu_id in parse_sink function */
			parse_err = !parse_sink(&config->sink_bin_sub_bin_configs[config->num_sink_sub_bins], group_name);
			if(config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].enable)
			{
				config->num_sink_sub_bins++;
			}
		}
		else if(starts_with(group_name, CONFIG_GROUP_MSG_CONSUMER))
		{
			if(config->num_message_consumers == MAX_MESSAGE_CONSUMERS)
			{
				TADS_ERR_MSG_V("App supports max %ld consumers", MAX_MESSAGE_CONSUMERS);
				success = false;
				goto done;
			}
			parse_err = !parse_msgconsumer(&config->message_consumer_configs[config->num_message_consumers], group_name);

			if(config->message_consumer_configs[config->num_message_consumers].enable)
			{
				config->num_message_consumers++;
			}
		}
		else if(group_name == CONFIG_GROUP_TILED_DISPLAY)
		{
			/** set gpu_id for tiled display component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->tiled_display_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for tiled display component is present,
			 * it will override the value set using global_gpu_id in parse_tiled_display function */
			parse_err = !parse_tiled_display(&config->tiled_display_config);
		}
		else if(group_name == CONFIG_GROUP_IMG_SAVE)
		{
			/** set gpu_id for image save component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->image_save_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for image save component is present,
			 * it will override the value set using global_gpu_id in parse_image_save function */
			parse_err = !parse_image_save(&config->image_save_config, group_name);
		}
		else if(group_name == CONFIG_GROUP_MSG_CONVERTER)
		{
			parse_err = !parse_msgconv(&config->msg_conv_config, group_name);
		}
		else if(group_name == CONFIG_GROUP_TESTS)
		{
			parse_err = !parse_tests(config);
		}

		if(parse_err)
		{
			GST_CAT_ERROR(APP_CFG_PARSER_CAT, "Failed to parse '%s' group", group_name.data());
			goto done;
		}
	}

	/* Updating batch size when source list is enabled */
	if(config->source_list_enabled)
	{
		/* For streammux and pgie, batch size is set to number of sources */
		config->streammux_config.batch_size = config->num_source_sub_bins;
		config->primary_gie_config.batch_size = config->num_source_sub_bins;
		if(config->sgie_batch_size != 0)
		{
			for(i = 0; i < config->num_secondary_gie_sub_bins; i++)
			{
				config->secondary_gie_sub_bin_configs[i].batch_size = config->sgie_batch_size;
			}
		}
	}

	for(i = 0; i < config->num_secondary_gie_sub_bins; i++)
	{
		if(config->secondary_gie_sub_bin_configs[i].unique_id == config->primary_gie_config.unique_id)
		{
			TADS_ERR_MSG_V("Non-unique gie ids found");
			success = false;
			goto done;
		}
	}

	for(i = 0; i < config->num_secondary_gie_sub_bins; i++)
	{
		for(j = i + 1; j < config->num_secondary_gie_sub_bins; j++)
		{
			if(config->secondary_gie_sub_bin_configs[i].unique_id == config->secondary_gie_sub_bin_configs[j].unique_id)
			{
				TADS_ERR_MSG_V("Non unique gie id %d found", config->secondary_gie_sub_bin_configs[i].unique_id);
				success = false;
				goto done;
			}
		}
	}

	for(i = 0; i < config->num_source_sub_bins; i++)
	{
		SourceConfig *multi_source_config{ &config->multi_source_configs.at(i) };

		if(multi_source_config->type == SourceType::URI_MULTIPLE)
		{
			if(multi_source_config->num_sources < 1)
			{
				multi_source_config->num_sources = 1;
			}
			for(j = 1; j < multi_source_config->num_sources; j++)
			{
				if(config->num_source_sub_bins == MAX_SOURCE_BINS)
				{
					TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
					success = false;
					goto done;
				}

				config->multi_source_configs[config->num_source_sub_bins] = { *multi_source_config };
				config->multi_source_configs[config->num_source_sub_bins].type = SourceType::URI;
				config->multi_source_configs[config->num_source_sub_bins].uri =
						fmt::format(config->multi_source_configs[config->num_source_sub_bins].uri, j);
				config->num_source_sub_bins++;
			}
			multi_source_config->type = SourceType::URI;
			multi_source_config->uri = fmt::format(multi_source_config->uri, 0);
		}
	}
	success = true;

done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_yaml(AppConfig *config)
{
	bool parse_err{}, success{};
	std::ifstream input_file{};
	m_file_yml = YAML::LoadFile(m_file_path);

	/** App group parsing at top level to set global_gpu_id (if available)
	 * before any other group parsing */
	if(m_file_yml[CONFIG_GROUP_APP.data()])
	{
		parse_err = !parse_app_yaml(config);
	}

	for(YAML::const_iterator itr = m_file_yml.begin(); itr != m_file_yml.end(); ++itr)
	{
		auto group = itr->first.as<std::string>();
		YAML::Node node;
		if(group == CONFIG_GROUP_SOURCE)
		{
			node = m_file_yml[CONFIG_GROUP_SOURCE.data()];
			if(auto csv_file_node = node[CONFIG_GROUP_SOURCE_CSV_PATH.data()]; csv_file_node)
			{
				auto csv_file_path = csv_file_node.as<std::string>();
				std::string abs_csv_path{};
				get_absolute_file_path_yaml(m_file_path, csv_file_path, abs_csv_path);

				input_file.open(abs_csv_path);
				if(!input_file.is_open())
				{
					TADS_WARN_MSG_V("Couldn't open CSV file '%s'", abs_csv_path.c_str());
				}
				std::string line, temp;
				/* Separating header field and inserting as strings into the vector.
				 */
				getline(input_file, line);
				std::vector<std::string> headers = split_csv_entries(line);
				/*Parsing each csv entry as an input source */
				while(getline(input_file, line))
				{
					SourceConfig *multi_source_config;
					std::vector<std::string> source_values = split_csv_entries(line);

					if(config->num_source_sub_bins == MAX_SOURCE_BINS)
					{
						TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
						success = false;
						goto done;
					}
					size_t source_id{ config->num_source_sub_bins };
					multi_source_config = &config->multi_source_configs.at(source_id);
					/** set gpu_id for source component using global_gpu_id(if available) */
					if(config->global_gpu_id != -1)
					{
						multi_source_config->gpu_id = config->global_gpu_id;
					}
					/** if gpu_id for source component is present,
					 * it will override the value set using global_gpu_id in parse_source_yaml function */
					parse_err = !parse_source_yaml(multi_source_config, headers, source_values);
					if(multi_source_config->enable)
						config->num_source_sub_bins++;
				}
			}
			else
			{
				TADS_ERR_MSG_V("CSV file not specified\n");
				success = false;
				goto done;
			}
		}
		else if(group == CONFIG_GROUP_STREAMMUX)
		{
			/** set gpu_id for streammux component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->streammux_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for streammux component is present,
			 * it will override the value set using global_gpu_id in parse_streammux_yaml function */
			parse_err = !parse_streammux_yaml(&config->streammux_config);
		}
		else if(group == CONFIG_GROUP_OSD)
		{
			/** set gpu_id for osd component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->osd_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for osd component is present,
			 * it will override the value set using global_gpu_id in parse_osd_yaml function */
			parse_err = !parse_osd_yaml(&config->osd_config);
		}
		else if(group == CONFIG_GROUP_PREPROCESS)
		{
			parse_err = !parse_preprocess_yaml(&config->preprocess_config);
		}
		else if(group == CONFIG_GROUP_PRIMARY_GIE)
		{
			/** set gpu_id for primary gie component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->primary_gie_config.gpu_id = config->global_gpu_id;
				config->primary_gie_config.is_gpu_id_set = true;
			}
			/** if gpu_id for primary gie component is present,
			 * it will override the value set using global_gpu_id in parse_gie_yaml function */
			parse_err = !parse_gie_yaml(&config->primary_gie_config, group);
		}
		else if(group == CONFIG_GROUP_TRACKER)
		{
			/** set gpu_id for tracker component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->tracker_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for tracker component is present,
			 * it will override the value set using global_gpu_id in parse_tracker_yaml function */
			parse_err = !parse_tracker_yaml(&config->tracker_config);
		}
		else if(starts_with(group, CONFIG_GROUP_SECONDARY_GIE))
		{
			if(config->num_secondary_gie_sub_bins == MAX_SECONDARY_GIE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld secondary GIEs", MAX_SECONDARY_GIE_BINS);
				success = false;
				goto done;
			}
			/* set gpu_id for secondary gie component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].gpu_id = config->global_gpu_id;
				config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].is_gpu_id_set = true;
			}
			/** if gpu_id for secondary gie component is present,
			 * it will override the value set using global_gpu_id in parse_gie_yaml function */
			parse_err = !parse_gie_yaml(&config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins], group);
			if(config->secondary_gie_sub_bin_configs[config->num_secondary_gie_sub_bins].enable)
			{
				config->num_secondary_gie_sub_bins++;
			}
		}
		else if(starts_with(group, CONFIG_GROUP_SINK))
		{
			if(config->num_sink_sub_bins == MAX_SINK_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sinks", MAX_SINK_BINS);
				success = false;
				goto done;
			}

			node = m_file_yml[group];

			/* set gpu_id for sink component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1 && node[CONFIG_KEY_ENABLE.data()].as<bool>())
			{
				config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].encoder_config.gpu_id =
						config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].render_config.gpu_id = config->global_gpu_id;
			}
			/**  if gpu_id for sink component is present,
			 * it will override the value set using global_gpu_id in parse_sink_yaml function */
			parse_err = !parse_sink_yaml(&config->sink_bin_sub_bin_configs[config->num_sink_sub_bins], group);
			if(config->sink_bin_sub_bin_configs[config->num_sink_sub_bins].enable)
			{
				config->num_sink_sub_bins++;
			}
		}
		else if(starts_with(group, CONFIG_GROUP_MSG_CONSUMER))
		{
			if(config->num_message_consumers == MAX_MESSAGE_CONSUMERS)
			{
				TADS_ERR_MSG_V("App supports max %ld consumers", MAX_MESSAGE_CONSUMERS);
				success = FALSE;
				goto done;
			}
			parse_err = !parse_msgconsumer_yaml(&config->message_consumer_configs[config->num_message_consumers], group);

			if(config->message_consumer_configs[config->num_message_consumers].enable)
			{
				config->num_message_consumers++;
			}
		}
		else if(group == CONFIG_GROUP_TILED_DISPLAY)
		{
			/* set gpu_id for tiled display component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->tiled_display_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for tiled display component is present,
			 * it will override the value set using global_gpu_id in parse_tiled_display_yaml function */
			parse_err = !parse_tiled_display_yaml(&config->tiled_display_config);
		}
		else if(group == CONFIG_GROUP_IMG_SAVE)
		{
			/** set gpu_id for image save component using global_gpu_id(if available) */
			if(config->global_gpu_id != -1)
			{
				config->image_save_config.gpu_id = config->global_gpu_id;
			}
			/** if gpu_id for image save component is present,
			 * it will override the value set using global_gpu_id in parse_image_save_yaml function */
			parse_err = !parse_image_save_yaml(&config->image_save_config);
		}
		else if(group == CONFIG_GROUP_ANALYTICS)
		{
			parse_err = !parse_analytics_yaml(&config->analytics_config);
		}
		else if(group == CONFIG_GROUP_MSG_CONVERTER)
		{
			parse_err = !parse_msgconv_yaml(&config->msg_conv_config);
		}
		else if(group == CONFIG_GROUP_TESTS)
		{
			parse_err = !parse_tests_yaml(config);
		}

		if(parse_err)
		{
			TADS_ERR_MSG_V("Failed parsing %s", group.c_str());
			goto done;
		}
	}
	/* Updating batch size when source list is enabled */
	/* if (config->source_list_enabled == true) {
			// For streammux and pgie, batch size is set to number of sources
			config->streammux_config.batch_size = config->num_source_sub_bins;
			config->primary_gie_config.batch_size = config->num_source_sub_bins;
			if (config->sgie_batch_size != 0) {
					for (i = 0; i < config->num_secondary_gie_sub_bins; i++) {
							config->secondary_gie_sub_bin_config[i].batch_size = config->sgie_batch_size;
					}
			}
	} */
	uint i, j;
	for(i = 0; i < config->num_secondary_gie_sub_bins; i++)
	{
		if(config->secondary_gie_sub_bin_configs[i].unique_id == config->primary_gie_config.unique_id)
		{
			TADS_ERR_MSG_V("Non unique gie ids found");
			success = false;
			goto done;
		}
	}

	for(i = 0; i < config->num_secondary_gie_sub_bins; i++)
	{
		for(j = i + 1; j < config->num_secondary_gie_sub_bins; j++)
		{
			if(config->secondary_gie_sub_bin_configs[i].unique_id == config->secondary_gie_sub_bin_configs[j].unique_id)
			{
				TADS_ERR_MSG_V("Non unique gie id %d found", config->secondary_gie_sub_bin_configs[i].unique_id);
				success = false;
				goto done;
			}
		}
	}

	for(i = 0; i < config->num_source_sub_bins; i++)
	{
		SourceConfig *multi_source_config{ &config->multi_source_configs.at(i) };

		if(multi_source_config->type == SourceType::URI_MULTIPLE)
		{
			if(multi_source_config->num_sources < 1)
			{
				multi_source_config->num_sources = 1;
			}
			for(j = 1; j < multi_source_config->num_sources; j++)
			{
				if(config->num_source_sub_bins == MAX_SOURCE_BINS)
				{
					TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
					success = false;
					goto done;
				}

				config->multi_source_configs[config->num_source_sub_bins] = { *multi_source_config };
				config->multi_source_configs[config->num_source_sub_bins].type = SourceType::URI;
				config->multi_source_configs[config->num_source_sub_bins].uri =
						fmt::format(config->multi_source_configs[config->num_source_sub_bins].uri, j);
				config->num_source_sub_bins++;
			}
			multi_source_config->type = SourceType::URI;
			multi_source_config->uri = fmt::format(multi_source_config->uri, 0);
		}
	}

	success = true;
done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_app(AppConfig *config)
{
	bool success{};
	GError *error{};
	const char *group_name{ CONFIG_GROUP_APP.data() };

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, group_name, nullptr, &error);
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group_name);
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT)
		{
			config->enable_perf_measurement = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable_perf_measurement);
#endif
		}
		else if(key == CONFIG_GROUP_APP_ENABLE_FILE_LOOP)
		{
			config->file_loop = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->file_loop);
#endif
		}
		else if(key == CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL)
		{
			config->perf_measurement_interval_sec = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->perf_measurement_interval_sec);
#endif
		}
		else if(key == CONFIG_GROUP_APP_OUTPUT_DIR)
		{
			auto output_dir_path = glib::key_file_get_string(m_key_file, group_name, key, &error);
			config->output_dir_path = get_absolute_file_path(m_file_path, output_dir_path);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->output_dir_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_APP_GIE_OUTPUT_DIR)
		{
			auto bbox_dir_path = glib::key_file_get_string(m_key_file, group_name, key, &error);
			config->bbox_dir_path = get_absolute_file_path(m_file_path, bbox_dir_path);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->bbox_dir_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_APP_GIE_TRACK_OUTPUT_DIR)
		{
			config->kitti_track_dir_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group_name, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->kitti_track_dir_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_APP_REID_TRACK_OUTPUT_DIR)
		{
			config->reid_track_dir_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group_name, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->kitti_track_dir_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_APP_GLOBAL_GPU_ID)
		{
			/** App Level GPU ID is set here if it is present in APP LEVEL config group
			 * if gpu_id prop is not set for any component, this global_gpu_id will be used */
			config->global_gpu_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->global_gpu_id);
#endif
		}
		else if(key == CONFIG_GROUP_APP_TERMINATED_TRACK_OUTPUT_DIR)
		{
			config->terminated_track_output_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group_name, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->terminated_track_output_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_APP_SHADOW_TRACK_OUTPUT_DIR)
		{
			config->shadow_track_output_path = get_absolute_file_path(
					m_file_path,
					glib::key_file_get_string(m_key_file, group_name, CONFIG_GROUP_APP_SHADOW_TRACK_OUTPUT_DIR, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->shadow_track_output_path.c_str());
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group_name);
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_app_yaml(AppConfig *config)
{
	bool success{ true };
	const auto group = CONFIG_GROUP_APP;
	auto cfg_group = m_file_yml[group.data()];
	const char *group_name{ group.data() };

	for(YAML::const_iterator itr = cfg_group.begin(); itr != cfg_group.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT)
		{
			config->enable_perf_measurement = itr->second.as<bool>();
		}
		if(key == CONFIG_GROUP_APP_ENABLE_FILE_LOOP)
		{
			config->file_loop = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL)
		{
			config->perf_measurement_interval_sec = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_APP_GIE_OUTPUT_DIR)
		{
			auto file_path = itr->second.as<std::string>();
			get_absolute_file_path_yaml(m_file_path, file_path, config->bbox_dir_path);
		}
		else if(key == CONFIG_GROUP_APP_GIE_TRACK_OUTPUT_DIR)
		{
			auto file_path = itr->second.as<std::string>();
			get_absolute_file_path_yaml(m_file_path, file_path, config->kitti_track_dir_path);
		}
		else if(key == CONFIG_GROUP_APP_REID_TRACK_OUTPUT_DIR)
		{
			auto file_path = itr->second.as<std::string>();
			get_absolute_file_path_yaml(m_file_path, file_path, config->reid_track_dir_path);
		}
		else if(key == CONFIG_GROUP_APP_GLOBAL_GPU_ID)
		{
			/** App Level GPU ID is set here if it is present in APP LEVEL config group
			 * if gpu_id prop is not set for any component, this global_gpu_id will be used */
			config->global_gpu_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_APP_TERMINATED_TRACK_OUTPUT_DIR)
		{
			auto file_path = itr->second.as<std::string>();
			get_absolute_file_path_yaml(m_file_path, file_path, config->terminated_track_output_path);
		}
		else if(key == CONFIG_GROUP_APP_SHADOW_TRACK_OUTPUT_DIR)
		{
			auto file_path = itr->second.as<std::string>();
			get_absolute_file_path_yaml(m_file_path, file_path, config->shadow_track_output_path);
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.c_str(), group_name);
		}
	}

	return success;
}

bool ConfigParser::parse_source(SourceConfig *config, std::string_view group)
{
	std::vector<std::string> keys;
	bool success{};
	GError *error{};
	static GList *camera_id_list{};

	if(group == CONFIG_GROUP_SOURCE_ALL)
	{
		if(!glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error) || error != nullptr)
		{
			return true;
		}

		// TODO: Check for possible bugs
		char *source_id_start_ptr = const_cast<char *>(group.begin());
		char *source_id_end_ptr{};
		config->camera_id = g_ascii_strtoull(source_id_start_ptr, &source_id_end_ptr, 10);

		config->rtsp_reconnect_attempts = -1;

		// Source group name should be of the form [source<%u>]. If
		// *source_id_end_ptr is not the string terminating character '\0' or if
		// the pointer has the same value as source_id_start_ptr, then the group
		// name does not conform to the specs.
		if(source_id_start_ptr == source_id_end_ptr || *source_id_end_ptr != '\0')
		{
			TADS_ERR_MSG_V("Source group \"'%s'\" is not in the form \"[source<%%d>]\"", group.data());
			return false;
		}
		// Check if a source with same source_id has already been parsed.
		if(g_list_find(camera_id_list, GUINT_TO_POINTER(config->camera_id)) != nullptr)
		{
			TADS_ERR_MSG_V("Did not parse source group \"'%s'\". Another source group"
										 " with source-id %d already exists",
										 group.data(), config->camera_id);
			return false;
		}
		camera_id_list = g_list_prepend(camera_id_list, GUINT_TO_POINTER(config->camera_id));
	}

	keys = glib::key_file_get_keys(m_key_file, group, nullptr, &error);
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->enable);
#endif
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->gpu_id);
#endif
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			uint nvbuf_memory_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), nvbuf_memory_type);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_TYPE)
		{
			config->type = static_cast<SourceType>(glib::key_file_get_integer(m_key_file, group, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->type);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_WIDTH)
		{
			config->source_width = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->source_width);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_HEIGHT)
		{
			config->source_height = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->source_height);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_FPS_N)
		{
			config->source_fps_n = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->source_fps_n);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_FPS_D)
		{
			config->source_fps_d = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->source_fps_d);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_CSI_SID)
		{
			config->camera_csi_sensor_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->camera_csi_sensor_id);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE)
		{
			config->camera_v4l2_dev_node = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), (int)config->camera_v4l2_dev_node);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_UDP_BUFFER_SIZE)
		{
			config->udp_buffer_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), (int)config->udp_buffer_size);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_VIDEO_FORMAT)
		{
			config->video_format = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->video_format.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_URI)
		{
			auto uri = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			if(starts_with(uri, "file://"))
			{
				uri = uri.substr(7, uri.length() - 7);
				config->uri = fmt::format("file://{}", get_absolute_file_path(m_file_path, uri));
			}
			else
			{
				config->uri = uri;
			}
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->uri.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_LATENCY)
		{
			config->latency = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->latency);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_SOURCES)
		{
			config->num_sources = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			if(config->num_sources < 1)
			{
				config->num_sources = 1;
			}
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->num_sources);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES)
		{
			config->num_decode_surfaces = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->num_decode_surfaces);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES)
		{
			config->num_extra_surfaces = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->num_extra_surfaces);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL)
		{
			config->drop_frame_interval = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->drop_frame_interval);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_ID)
		{
			config->camera_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->camera_id);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_RTSP_RECONNECT_INTERVAL_SEC)
		{
			config->rtsp_reconnect_interval_sec = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->rtsp_reconnect_interval_sec);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_RTSP_RECONNECT_ATTEMPTS)
		{
			config->rtsp_reconnect_attempts = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->rtsp_reconnect_attempts);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_INTRA_DECODE)
		{
			config->intra_decode = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->intra_decode);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_LOW_LATENCY_DECODE)
		{
			config->low_latency_mode = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->low_latency_mode);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_CUDADEC_MEMTYPE)
		{
			config->cuda_memory_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->cuda_memory_type);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SELECT_RTP_PROTOCOL)
		{
			config->select_rtp_protocol = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->select_rtp_protocol);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_ID)
		{
			config->source_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->source_id);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_ENABLE)
		{
			config->smart_record = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_record);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DIRPATH)
		{
			config->dir_path = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

			if(access(config->dir_path.c_str(), W_OK))
			{
				if(errno == ENOENT || errno == ENOTDIR)
				{
					TADS_ERR_MSG_V("Directory (%s) doesn't exist", config->dir_path.c_str());
				}
				else if(errno == EACCES)
				{
					TADS_ERR_MSG_V("No write permission in %s", config->dir_path.c_str());
				}
				goto done;
			}

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->dir_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_FILE_PREFIX)
		{
			config->file_prefix = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->file_prefix.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE_LEGACY)
		{
			TADS_WARN_MSG_V("Deprecated config '%s' used in group '%s'. Use '%s' instead", key.data(), group.data(),
											CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE.data());

			config->smart_rec_cache_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_cache_size);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE)
		{
			config->smart_rec_cache_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_cache_size);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CONTAINER)
		{
			config->smart_rec_container = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_container);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_START_TIME)
		{
			config->smart_rec_start_time = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_start_time);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DEFAULT_DURATION)
		{
			config->smart_rec_def_duration = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_def_duration);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DURATION)
		{
			config->smart_rec_duration = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_duration);
#endif
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_INTERVAL)
		{
			config->smart_rec_interval = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->smart_rec_interval);
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_source_yaml(SourceConfig *config, std::vector<std::string> headers,
																		 std::vector<std::string> source_values)
{
	bool success{};
	const char *group_name{ CONFIG_GROUP_SOURCE.data() };

	for(uint i{}; i < headers.size(); i++)
	{
		std::string key = headers.at(i);

		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			int nvbuf_memory_type = std::stoul(source_values[i]);
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
		}
		else if(key == CONFIG_GROUP_SOURCE_TYPE)
		{
			config->type = static_cast<SourceType>(std::stoi(source_values[i]));
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_WIDTH)
		{
			config->source_width = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_HEIGHT)
		{
			config->source_height = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_FPS_N)
		{
			config->source_fps_n = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_FPS_D)
		{
			config->source_fps_d = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_CSI_SID)
		{
			config->camera_csi_sensor_id = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE)
		{
			config->camera_v4l2_dev_node = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_UDP_BUFFER_SIZE)
		{
			config->udp_buffer_size = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_VIDEO_FORMAT)
		{
			config->video_format = source_values[i];
		}
		else if(key == CONFIG_GROUP_SOURCE_URI)
		{
			auto temp = source_values[i];
			char *uri = new char[G_PATH_MAX];
			std::strncpy(uri, temp.c_str(), G_PATH_MAX - 1);
			char *str;
			if(g_str_has_prefix(uri, "file://"))
			{
				str = g_strdup(uri + 7);
				config->uri = new char[G_PATH_MAX];
				get_absolute_file_path_yaml(m_file_path, str, config->uri);
				config->uri = fmt::format("file://{}", config->uri);
				delete[] uri;
				delete[] str;
			}
			else
			{
				config->uri = uri;
			}
		}
		else if(key == CONFIG_GROUP_SOURCE_LATENCY)
		{
			config->latency = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_SOURCES)
		{
			config->num_sources = std::stoul(source_values[i]);
			if(config->num_sources < 1)
			{
				config->num_sources = 1;
			}
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES)
		{
			config->num_decode_surfaces = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES)
		{
			config->num_extra_surfaces = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL)
		{
			config->drop_frame_interval = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CAMERA_ID)
		{
			config->camera_id = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_RTSP_RECONNECT_INTERVAL_SEC)
		{
			config->rtsp_reconnect_interval_sec = std::stoi(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_RTSP_RECONNECT_ATTEMPTS)
		{
			config->rtsp_reconnect_attempts = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_INTRA_DECODE)
		{
			config->intra_decode = (bool)std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_CUDADEC_MEMTYPE)
		{
			config->cuda_memory_type = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SELECT_RTP_PROTOCOL)
		{
			config->select_rtp_protocol = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_ID)
		{
			config->source_id = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_ENABLE)
		{
			config->smart_record = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DIRPATH)
		{
			config->dir_path = source_values[i];
			if(access(config->dir_path.c_str(), 2))
			{
				if(errno == ENOENT || errno == ENOTDIR)
				{
					TADS_ERR_MSG_V("Directory (%s) doesn't exist", config->dir_path.c_str());
				}
				else if(errno == EACCES)
				{
					TADS_ERR_MSG_V("No write permission in %s", config->dir_path.c_str());
				}
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_FILE_PREFIX)
		{
			config->file_prefix = source_values[i];
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE_LEGACY)
		{
			TADS_WARN_MSG_V("Deprecated config smart-rec-video-cache used in source. Use smart-rec-cache instead");
			config->smart_rec_cache_size = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE)
		{
			config->smart_rec_cache_size = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_CONTAINER)
		{
			config->smart_rec_container = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_START_TIME)
		{
			config->smart_rec_start_time = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DEFAULT_DURATION)
		{
			config->smart_rec_def_duration = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_DURATION)
		{
			config->smart_rec_duration = std::stoul(source_values[i]);
		}
		else if(key == CONFIG_GROUP_SOURCE_SMART_RECORD_INTERVAL)
		{
			config->smart_rec_interval = std::stoul(source_values[i]);
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_source_list(AppConfig *config)
{
	bool success{};
	GError *error{};
	gsize num_strings;

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, CONFIG_GROUP_SOURCE_LIST, nullptr, &error);
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_GROUP_SOURCE_LIST_NUM_SOURCE_BINS)
		{
			config->total_num_sources = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_SOURCE_LIST,
																														 CONFIG_GROUP_SOURCE_LIST_NUM_SOURCE_BINS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_URI_LIST)
		{
			config->uri_list = glib::key_file_get_string_list(m_key_file, CONFIG_GROUP_SOURCE_LIST,
																												CONFIG_GROUP_SOURCE_LIST_URI_LIST, &num_strings, &error);
			if(num_strings > MAX_SOURCE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
				goto done;
			}
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_SENSOR_ID_LIST)
		{
			config->sensor_id_list = glib::key_file_get_string_list(
					m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_SENSOR_ID_LIST, &num_strings, &error);
			if(num_strings > MAX_SOURCE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
				goto done;
			}
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_SENSOR_NAME_LIST)
		{
			config->sensor_name_list = glib::key_file_get_string_list(
					m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_SENSOR_NAME_LIST, &num_strings, &error);
			if(num_strings > MAX_SOURCE_BINS)
			{
				TADS_ERR_MSG_V("App supports max %ld sources", MAX_SOURCE_BINS);
				goto done;
			}
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_USE_NVMULTIURISRCBIN)
		{
			config->use_nvmultiurisrcbin = glib::key_file_get_boolean(m_key_file, CONFIG_GROUP_SOURCE_LIST,
																																CONFIG_GROUP_SOURCE_LIST_USE_NVMULTIURISRCBIN, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_STREAM_NAME_DISPLAY)
		{
			config->stream_name_display = glib::key_file_get_boolean(m_key_file, CONFIG_GROUP_SOURCE_LIST,
																															 CONFIG_GROUP_SOURCE_LIST_STREAM_NAME_DISPLAY, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_HTTP_IP)
		{
			config->http_ip =
					glib::key_file_get_string(m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_HTTP_IP, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_HTTP_PORT)
		{
			config->http_port =
					glib::key_file_get_string(m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_HTTP_PORT, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_LIST_MAX_BATCH_SIZE)
		{
			config->max_batch_size = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_SOURCE_LIST,
																													CONFIG_GROUP_SOURCE_LIST_MAX_BATCH_SIZE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SOURCE_SGIE_BATCH_SIZE)
		{
			config->sgie_batch_size =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_SGIE_BATCH_SIZE, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), CONFIG_GROUP_SOURCE_LIST.data());
		}
	}

	if(glib::key_file_has_key(m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_URI_LIST, &error))
	{
		if(glib::key_file_has_key(m_key_file, CONFIG_GROUP_SOURCE_LIST, CONFIG_GROUP_SOURCE_LIST_NUM_SOURCE_BINS, &error))
		{
			if(num_strings != config->total_num_sources)
			{
				TADS_ERR_MSG_V("Mismatch in URIs provided and num-source-bins.");
				goto done;
			}
		}
		else
		{
			config->total_num_sources = num_strings;
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_streammux(StreammuxConfig *config)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, CONFIG_GROUP_STREAMMUX, nullptr, &error);
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_GROUP_STREAMMUX_WIDTH)
		{
			config->width =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_WIDTH, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_HEIGHT)
		{
			config->height =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_HEIGHT, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_KEY_GPU_ID, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_ENABLE_PADDING)
		{
			config->enable_padding =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_ENABLE_PADDING, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_FRAME_DURATION)
		{
			config->frame_duration =
					glib::key_file_get_uint64(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_FRAME_DURATION, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_BUFFER_POOL_SIZE)
		{
			config->buffer_pool_size = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX,
																														CONFIG_GROUP_STREAMMUX_BUFFER_POOL_SIZE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_BATCH_SIZE)
		{
			config->batch_size =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_BATCH_SIZE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_LIVE_SOURCE)
		{
			config->live_source =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_LIVE_SOURCE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_ATTACH_SYS_TS_AS_NTP)
		{
			config->attach_sys_ts_as_ntp = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX,
																																CONFIG_GROUP_STREAMMUX_ATTACH_SYS_TS_AS_NTP, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_STREAM_RESET)
		{
			config->frame_num_reset_on_stream_reset = glib::key_file_get_boolean(
					m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_STREAM_RESET, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT)
		{
			config->batched_push_timeout = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX,
																																CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_KEY_CUDA_MEMORY_TYPE, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_CONFIG_FILE_PATH)
		{
			config->config_file_path = get_absolute_file_path(
					m_file_path, glib::key_file_get_string(m_key_file, CONFIG_GROUP_STREAMMUX,
																								 CONFIG_GROUP_STREAMMUX_CONFIG_FILE_PATH, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_SYNC_INPUTS)
		{
			config->sync_inputs =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_SYNC_INPUTS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_MAX_LATENCY)
		{
			config->max_latency =
					glib::key_file_get_uint64(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_MAX_LATENCY, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_EOS)
		{
			config->frame_num_reset_on_eos = glib::key_file_get_boolean(
					m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_EOS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_ASYNC_PROCESS)
		{
			config->async_process =
					glib::key_file_get_boolean(m_key_file, CONFIG_GROUP_STREAMMUX, CONFIG_GROUP_STREAMMUX_ASYNC_PROCESS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_DROP_PIPELINE_EOS)
		{
			config->no_pipeline_eos = glib::key_file_get_boolean(m_key_file, CONFIG_GROUP_STREAMMUX,
																													 CONFIG_GROUP_STREAMMUX_DROP_PIPELINE_EOS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_STREAMMUX_NUM_SURFACES_PER_FRAME)
		{
			config->num_surface_per_frame = glib::key_file_get_integer(m_key_file, CONFIG_GROUP_STREAMMUX,
																																 CONFIG_GROUP_STREAMMUX_NUM_SURFACES_PER_FRAME, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), CONFIG_GROUP_STREAMMUX.data());
		}
	}

	config->is_parsed = true;

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_streammux_yaml(StreammuxConfig *config)
{
	bool success{};
	const char *group_name{ CONFIG_GROUP_STREAMMUX.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto param_key = itr->first.as<std::string>();
		if(param_key == CONFIG_GROUP_STREAMMUX_WIDTH)
		{
			config->width = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_HEIGHT)
		{
			config->height = itr->second.as<int>();
		}
		else if(param_key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_LIVE_SOURCE)
		{
			config->live_source = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_BUFFER_POOL_SIZE)
		{
			config->buffer_pool_size = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_BATCH_SIZE)
		{
			config->batch_size = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT)
		{
			config->batched_push_timeout = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_ENABLE_PADDING)
		{
			config->enable_padding = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_FRAME_DURATION)
		{
			config->frame_duration = itr->second.as<uint64_t>();
		}
		else if(param_key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(itr->second.as<uint>());
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_CONFIG_FILE_PATH)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->config_file_path = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse %s in %s", CONFIG_GROUP_STREAMMUX_CONFIG_FILE_PATH.data(), group_name);
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_COMPUTE_HW)
		{
			config->compute_hw = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_ATTACH_SYS_TS)
		{
			config->attach_sys_ts_as_ntp = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_STREAM_RESET)
		{
			config->frame_num_reset_on_stream_reset = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_EOS)
		{
			config->frame_num_reset_on_eos = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_NUM_SURFACES_PER_FRAME)
		{
			config->num_surface_per_frame = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_INTERP_METHOD)
		{
			config->interpolation_method = itr->second.as<int>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_SYNC_INPUTS)
		{
			config->sync_inputs = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_MAX_LATENCY)
		{
			config->max_latency = itr->second.as<uint64_t>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_ASYNC_PROCESS)
		{
			config->async_process = itr->second.as<bool>();
		}
		else if(param_key == CONFIG_GROUP_STREAMMUX_DROP_PIPELINE_EOS)
		{
			config->no_pipeline_eos = itr->second.as<bool>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", param_key.c_str(), group_name);
			goto done;
		}
	}

	config->is_parsed = true;
	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_gie(GieConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	if(!glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error) || error != nullptr)
		return true;

	config->bbox_border_color_table = g_hash_table_new(nullptr, nullptr);
	config->bbox_bg_color_table = g_hash_table_new(nullptr, nullptr);
	config->bbox_border_color = osd_color("0;1;0;1");

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, group, nullptr, &error);
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable);
#endif
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_gpu_id_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->gpu_id);
#endif
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			uint nvbuf_memory_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), nvbuf_memory_type);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_INPUT_TENSOR_META)
		{
			config->input_tensor_meta = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->input_tensor_meta);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION)
		{
			gsize length;
			config->operate_on_classes = glib::key_file_get_integer_list(m_key_file, group, key, &length, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			std::string class_ids;
			for(int class_id : config->operate_on_classes)
				class_ids += std::to_string(class_id) + ":";
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), class_ids.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_CLASS_IDS_FOR_FILTER)
		{
			gsize length;
			config->filter_out_classes = glib::key_file_get_integer_list(m_key_file, group, key, &length, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			std::string class_ids;
			for(int class_id : config->filter_out_classes)
				class_ids += std::to_string(class_id) + ":";
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), class_ids.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_BATCH_SIZE)
		{
			config->batch_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_batch_size_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->batch_size);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_MODEL_ENGINE)
		{
			config->model_engine_file_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->model_engine_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_PLUGIN_TYPE)
		{
			uint plugin_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			config->plugin_type = static_cast<GiePluginType>(plugin_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), plugin_type);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_FRAME_SIZE)
		{
			config->frame_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_frame_size_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->batch_size);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_HOP_SIZE)
		{
			config->hop_size = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_hop_size_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->hop_size);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_LABEL_FILE)
		{
			config->label_file_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->label_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_CONFIG_FILE)
		{
			config->config_file_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->config_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_INTERVAL)
		{
			config->interval = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_interval_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->interval);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_UNIQUE_ID)
		{
			config->unique_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_unique_id_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->unique_id);
#endif
		}
		else if(key == CONFIG_GROUP_GIE_ID_FOR_OPERATION)
		{
			config->operate_on_gie_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->is_operate_on_gie_id_set = true;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->operate_on_gie_id);
#endif
		}
		else if(starts_with(key, CONFIG_GROUP_GIE_BBOX_BORDER_COLOR))
		{
			NvOSD_ColorParams *clr_params;
			const char *key1{ key.substr(CONFIG_GROUP_GIE_BBOX_BORDER_COLOR.length() - 1).data() };
			char *endptr{};
			gint64 class_index{ -1 };

			/* Check if the key is specified for a particular class or for all classes.
			 * For generic key "bbox-border-color", strlen (key1) will return 0 and·
			 * class_index will be -1.
			 * For class-specific key "bbox-border-color<class-id>", strlen (key1)
			 * will return a positive value and·class_index will have a value >= 0.
			 */
			if(strlen(key1) > 0)
			{
				class_index = std::strtoll(key1, nullptr, 10);
				if(class_index == 0 && endptr == key1)
				{
					TADS_WARN_MSG_V("BBOX colors should be specified with key '%s%%d'",
													CONFIG_GROUP_GIE_BBOX_BORDER_COLOR.data());
					continue;
				}
			}

			gsize length = 0;
			double *list = glib::key_file_get_double_list(m_key_file, group, key, &length, &error);
			CHECK_ERROR(error)
			if(length != 4)
			{
				TADS_ERR_MSG_V("Number of Color params should be exactly 4 "
											 "floats {r, g, b, a} between 0 and 1");
				goto done;
			}

			if(class_index == -1)
			{
				clr_params = &config->bbox_border_color;
			}
			else
			{
				clr_params = new NvOSD_ColorParams;
				// TODO: Watch for to_string::data
				g_hash_table_insert(config->bbox_border_color_table, std::to_string(class_index).data(), clr_params);
			}

			clr_params->red = list[0];
			clr_params->green = list[1];
			clr_params->blue = list[2];
			clr_params->alpha = list[3];
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s'", key.data());
#endif
		}
		else if(starts_with(key, CONFIG_GROUP_GIE_BBOX_BG_COLOR))
		{
			NvOSD_ColorParams *clr_params;
			const char *key1{ key.substr(CONFIG_GROUP_GIE_BBOX_BG_COLOR.size()).data() };
			gint64 class_index = -1;

			/* Check if the key is specified for a particular class or for all classes.
			 * For generic key "bbox-bg-color", strlen (key1) will return 0 and·
			 * class_index will be -1.
			 * For class-specific key "bbox-bg-color<class-id>", strlen (key1)
			 * will return a positive value and·class_index will have a value >= 0.
			 */
			if(std::strlen(key1) > 0)
			{
				class_index = std::strtoll(key1, nullptr, 10);
				if(class_index == 0 && key1 == nullptr)
				{
					TADS_WARN_MSG_V("BBOX background colors should be specified with key '%s%%d'",
													CONFIG_GROUP_GIE_BBOX_BG_COLOR.data());
					continue;
				}
			}

			gsize length{};
			double *list = glib::key_file_get_double_list(m_key_file, group, key, &length, &error);
			CHECK_ERROR(error)
			if(length != 4)
			{
				TADS_ERR_MSG_V("Number of Color params should be exactly 4 "
											 "floats {r, g, b, a} between 0 and 1");
				goto done;
			}

			if(class_index == -1)
			{
				clr_params = &config->bbox_bg_color;
				config->have_bg_color = true;
			}
			else
			{
				clr_params = new NvOSD_ColorParams;
				g_hash_table_insert(config->bbox_bg_color_table, class_index + (char *)nullptr, clr_params);
			}

			clr_params->red = list[0];
			clr_params->green = list[1];
			clr_params->blue = list[2];
			clr_params->alpha = list[3];
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s'", key.data());
#endif
		}
		else if(key == CONFIG_GROUP_GIE_RAW_OUTPUT_DIR)
		{
			config->raw_output_directory =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->raw_output_directory.c_str());
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}
	if(config->enable && !config->label_file_path.empty() && !parse_labels_file(config))
	{
		TADS_ERR_MSG_V("Failed while parsing label file '%s'", config->label_file_path.c_str());
		goto done;
	}
	if(config->config_file_path.empty())
	{
		TADS_ERR_MSG_V("Config file not provided for group '%s'", group.data());
		goto done;
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_gie_yaml(GieConfig *config, std::string_view group)
{
	bool success{};
	const char *group_name{ group.data() };
	auto node = m_file_yml[group_name];

	if(node[CONFIG_KEY_ENABLE.data()])
	{
		if(!node[CONFIG_KEY_ENABLE.data()].as<bool>())
			return true;
	}

	config->bbox_border_color_table = g_hash_table_new(nullptr, nullptr);
	config->bbox_bg_color_table = g_hash_table_new(nullptr, nullptr);
	config->bbox_border_color = { 1, 0, 0, 1 };
	std::string border_str{ "bbox-border-color" };
	std::string bg_str{ "bbox-bg-color" };

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();

		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_GIE_INPUT_TENSOR_META)
		{
			config->input_tensor_meta = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION)
		{
			auto class_ids_str = itr->second.as<std::string>();
			std::vector<std::string> class_id_str_list = split_string(class_ids_str);
			for(const auto &class_id_str : class_id_str_list)
			{
				config->operate_on_classes.push_back(std::stoi(class_id_str));
			}
		}
		else if(key == CONFIG_GROUP_GIE_CLASS_IDS_FOR_FILTER)
		{
			auto class_ids_str = itr->second.as<std::string>();
			std::vector<std::string> class_id_str_list = split_string(class_ids_str);
			for(const auto &class_id_str : class_id_str_list)
			{
				config->filter_out_classes.push_back(std::stoi(class_id_str));
			}
		}
		else if(key == CONFIG_GROUP_GIE_BATCH_SIZE)
		{
			config->batch_size = itr->second.as<uint>();
			config->is_batch_size_set = true;
		}
		else if(key == CONFIG_GROUP_GIE_MODEL_ENGINE)
		{
			auto file_path = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, file_path, config->model_engine_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_GIE_PLUGIN_TYPE)
		{
			config->plugin_type = static_cast<GiePluginType>(itr->second.as<uint>());
		}
		else if(key == CONFIG_GROUP_GIE_FRAME_SIZE)
		{
			config->frame_size = itr->second.as<uint>();
			config->is_frame_size_set = true;
		}
		else if(key == CONFIG_GROUP_GIE_HOP_SIZE)
		{
			config->hop_size = itr->second.as<uint>();
			config->is_hop_size_set = true;
		}
		else if(key == CONFIG_GROUP_GIE_LABEL_FILE)
		{
			auto file_path = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, file_path, config->label_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_GIE_CONFIG_FILE)
		{
			auto file_path = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, file_path, config->config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse %s in %s", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_GIE_INTERVAL)
		{
			config->interval = itr->second.as<uint>();
			config->is_interval_set = true;
		}
		else if(key == CONFIG_GROUP_GIE_UNIQUE_ID)
		{
			config->unique_id = itr->second.as<uint>();
			config->is_unique_id_set = true;
		}
		else if(key == CONFIG_GROUP_GIE_ID_FOR_OPERATION)
		{
			config->operate_on_gie_id = itr->second.as<int>();
			config->is_operate_on_gie_id_set = true;
		}
		else if(key.compare(0, border_str.size(), border_str) == 0)
		{
			NvOSD_ColorParams *clr_params;
			auto str = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(str);
			if(vec.size() != 4)
			{
				TADS_ERR_MSG_V("Number of Color params should be exactly 4 "
											 "floats {r, g, b, a} between 0 and 1");
				goto done;
			}

			gint64 class_index = -1;
			if(key != border_str)
			{
				class_index = std::stoi(key.substr(border_str.size()));
			}

			double list[4];
			for(uint i{}; i < 4; i++)
			{
				list[i] = std::stod(vec[i]);
			}

			if(class_index == -1)
			{
				clr_params = &config->bbox_border_color;
			}
			else
			{
				clr_params = new NvOSD_ColorParams;
				g_hash_table_insert(config->bbox_border_color_table, (char *)std::to_string(class_index).c_str(), clr_params);
			}

			clr_params->red = list[0];
			clr_params->green = list[1];
			clr_params->blue = list[2];
			clr_params->alpha = list[3];
		}
		else if(key.compare(0, bg_str.size(), bg_str) == 0)
		{
			NvOSD_ColorParams *clr_params;
			auto str = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(str);
			if(vec.size() != 4)
			{
				TADS_ERR_MSG_V("Number of Color params should be exactly 4 "
											 "floats {r, g, b, a} between 0 and 1");
				goto done;
			}

			gint64 class_index = -1;
			if(key != bg_str)
			{
				class_index = std::stoi(key.substr(bg_str.size()));
			}

			double list[4];
			for(unsigned int i = 0; i < 4; i++)
			{
				list[i] = std::stod(vec[i]);
			}

			if(class_index == -1)
			{
				clr_params = &config->bbox_bg_color;
				config->have_bg_color = true;
			}
			else
			{
				clr_params = new NvOSD_ColorParams;
				g_hash_table_insert(config->bbox_bg_color_table, (char *)std::to_string(class_index).c_str(), clr_params);
			}

			clr_params->red = list[0];
			clr_params->green = list[1];
			clr_params->blue = list[2];
			clr_params->alpha = list[3];
		}
		else if(key == CONFIG_GROUP_GIE_RAW_OUTPUT_DIR)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->raw_output_directory = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->raw_output_directory))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", key.c_str(), group_name);
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
			config->is_gpu_id_set = true;
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(itr->second.as<uint>());
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	if(config->enable && !config->label_file_path.empty() && !parse_labels_file(config))
	{
		TADS_ERR_MSG_V("Failed while parsing label file %s", config->label_file_path.c_str());
		goto done;
	}
	if(config->config_file_path.empty())
	{
		TADS_ERR_MSG_V("Config file not provided for group '%s'", group_name);
		goto done;
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_tracker(TrackerConfig *config)
{
	bool success{};
	GError *error{};
	const char *group_name{ CONFIG_GROUP_TRACKER.data() };

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, group_name, nullptr, &error);
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group_name.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable);
#endif
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->gpu_id);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_WIDTH)
		{
			config->tracker_width = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->tracker_width);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_HEIGHT)
		{
			config->tracker_height = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->tracker_height);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_SURFACE_TYPE)
		{
			config->tracking_surface_type = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->tracking_surface_type);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_LL_CONFIG_FILE)
		{
			// TODO: Refactor to C++
			auto temp = glib::key_file_get_string(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
			char **configFileList;
			std::string single_config_file_path;
			configFileList = g_strsplit(temp.c_str(), ";", 0);
			char *temp_list1, *temp_list2;
			if(g_strv_length(configFileList) == 1)
			{
				// These is a single config file
				config->ll_config_file = get_absolute_file_path(m_file_path, temp);
			}
			else
			{
				single_config_file_path = get_absolute_file_path(m_file_path, configFileList[0]);
				temp_list1 = g_strconcat(single_config_file_path.c_str(), ";", nullptr);
				for(int i = 1; i < (int)g_strv_length(configFileList); i++)
				{
					single_config_file_path = get_absolute_file_path(m_file_path, configFileList[i]);
					temp_list2 = g_strconcat(temp_list1, single_config_file_path.c_str(), ";", nullptr);
					g_free(temp_list1);
					temp_list1 = temp_list2;
				}
				config->ll_config_file = temp_list1;
			}

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->ll_config_file.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_LL_LIB_FILE)
		{
			config->ll_lib_file =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group_name, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->ll_lib_file.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_TRACKING_SURFACE_TYPE)
		{
			config->tracking_surface_type = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->tracking_surface_type);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_DISPLAY_TRACKING_ID)
		{
			config->display_tracking_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->display_tracking_id);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_TRACKING_ID_RESET_MODE)
		{
			uint tracking_id_reset_mode = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			if(0 <= tracking_id_reset_mode && tracking_id_reset_mode <= 3)
			{
				config->tracking_id_reset_mode = tracking_id_reset_mode;
			}
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), tracking_id_reset_mode);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_INPUT_TENSOR_META)
		{
			config->input_tensor_meta = glib::key_file_get_boolean(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->input_tensor_meta);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_TENSOR_META_GIE_ID)
		{
			config->input_tensor_gie_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->input_tensor_gie_id);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_COMPUTE_HW)
		{
			int compute_hw = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
			config->compute_hw = static_cast<TrackerComputeHW>(compute_hw);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), compute_hw);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_USER_META_POOL_SIZE)
		{
			config->user_meta_pool_size = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%u'", key.data(), config->user_meta_pool_size);
#endif
		}
		else if(key == CONFIG_GROUP_TRACKER_SUB_BATCHES)
		{
			config->sub_batches = glib::key_file_get_string(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->sub_batches.c_str());
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group_name);
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_tracker_yaml(TrackerConfig *config)
{
	bool success{};
	std::string_view group{ CONFIG_GROUP_TRACKER };
	const char *group_name{ group.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_TRACKER_WIDTH)
		{
			config->tracker_width = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_TRACKER_HEIGHT)
		{
			config->tracker_height = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_TRACKER_SURFACE_TYPE)
		{
			config->tracking_surface_type = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_TRACKER_LL_CONFIG_FILE)
		{
			std::string temp;
			std::stringstream string_stream{ itr->second.as<std::string>() };
			std::stringstream string_stream_out;
			char *str = new char[G_PATH_MAX];
			std::string str_out{};
			while(std::getline(string_stream, temp, ';'))
			{
				std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
				if(!get_absolute_file_path_yaml(m_file_path, str, str_out))
				{
					TADS_ERR_MSG_V("Could not parse '%s' in '%s'", key.c_str(), group_name);
					delete[] str;
					goto done;
				}
				string_stream_out << str_out << ";";
			}
			config->ll_config_file = g_strdup(string_stream_out.str().c_str());
			delete[] str;
		}
		else if(key == CONFIG_GROUP_TRACKER_LL_LIB_FILE)
		{
			auto file_path = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, file_path, config->ll_lib_file))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_TRACKER_DISPLAY_TRACKING_ID)
		{
			config->display_tracking_id = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_TRACKER_TRACKING_ID_RESET_MODE)
		{
			int tracking_id_reset_mode = itr->second.as<int>();
			if(0 <= tracking_id_reset_mode && tracking_id_reset_mode <= 3)
			{
				config->tracking_id_reset_mode = tracking_id_reset_mode;
			}
		}
		else if(key == CONFIG_GROUP_TRACKER_INPUT_TENSOR_META)
		{
			config->input_tensor_meta = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_TRACKER_TENSOR_META_GIE_ID)
		{
			config->input_tensor_gie_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_TRACKER_COMPUTE_HW)
		{
			config->compute_hw = static_cast<TrackerComputeHW>(itr->second.as<uint>());
		}
		else if(key == CONFIG_GROUP_TRACKER_USER_META_POOL_SIZE)
		{
			config->user_meta_pool_size = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_TRACKER_SUB_BATCHES)
		{
			config->sub_batches = itr->second.as<std::string>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_preprocess(PreProcessConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, group, nullptr, &error);
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_GIE_ID_FOR_OPERATION)
		{
			config->operate_on_gie_id =
					glib::key_file_get_integer(m_key_file, group, CONFIG_GROUP_GIE_ID_FOR_OPERATION, &error);
			CHECK_ERROR(error)
			config->is_operate_on_gie_id_set = true;
		}
		else if(key == CONFIG_GROUP_PREPROCESS_CONFIG_FILE)
		{
			config->config_file_path = get_absolute_file_path(
					m_file_path, glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_PREPROCESS_CONFIG_FILE, &error));
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_preprocess_yaml(PreProcessConfig *config)
{
	bool success{};

	const char *group_name{ CONFIG_GROUP_PREPROCESS.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_PREPROCESS_CONFIG_FILE)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->config_file_path = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in group '%s'", key.c_str(), group_name);
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_analytics(AnalyticsConfig *config)
{
	bool success{};
	GError *error{};
	const char *group_name{ CONFIG_GROUP_ANALYTICS.data() };

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group_name, nullptr, &error) };
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group_name.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable);
#endif
		}
		else if(key == CONFIG_GROUP_ANALYTICS_UNIQUE_ID)
		{
			config->unique_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->unique_id);
#endif
		}
		else if(key == CONFIG_GROUP_ANALYTICS_CONFIG_FILE)
		{
			config->config_file_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group_name, key, &error));
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->config_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_ANALYTICS_OUTPUT_PATH)
		{
			std::string output_path = glib::key_file_get_string(m_key_file, group_name, key, &error);
			config->output_path = get_absolute_file_path(m_file_path, output_path);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->output_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_ANALYTICS_DISTANCE)
		{
			config->lines_distance = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%f'", key.data(), config->lines_distance);
#endif
		}
		else if(key == CONFIG_GROUP_ANALYTICS_LP_MIN_LENGTH)
		{
			config->lp_min_length = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%f'", key.data(), config->lp_min_length);
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group_name '%s'", key.data(), group_name);
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_analytics_yaml(AnalyticsConfig *config)
{
	bool success{};
	const char *group_name{ CONFIG_GROUP_ANALYTICS.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_ANALYTICS_CONFIG_FILE)
		{
			auto temp = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, temp, config->config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in group '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_ANALYTICS_OUTPUT_PATH)
		{
			auto temp = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, temp, config->output_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in group '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_ANALYTICS_DISTANCE)
		{
			config->lines_distance = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_ANALYTICS_LP_MIN_LENGTH)
		{
			config->lp_min_length = itr->second.as<int>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_msgconv(SinkMsgConvBrokerConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group, nullptr, &error) };
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_CONFIG)
		{
			config->config_file_path = get_absolute_file_path(
					m_file_path, glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_CONFIG, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE)
		{
			config->conv_payload_type =
					glib::key_file_get_integer(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB)
		{
			config->conv_msg2p_lib = get_absolute_file_path(
					m_file_path, glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_COMP_ID)
		{
			config->conv_comp_id = glib::key_file_get_integer(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_COMP_ID, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR)
		{
			config->debug_payload_dir = get_absolute_file_path(
					m_file_path,
					glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS)
		{
			config->multiple_payloads =
					glib::key_file_get_boolean(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API)
		{
			config->conv_msg2p_new_api =
					glib::key_file_get_boolean(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL)
		{
			config->conv_frame_interval =
					glib::key_file_get_integer(m_key_file, group, CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_msgconv_yaml(SinkMsgConvBrokerConfig *config)
{
	return parse_msgconv_yaml(config, CONFIG_GROUP_MSG_CONVERTER);
}

bool ConfigParser::parse_msgconv_yaml(SinkMsgConvBrokerConfig *config, std::string_view group)
{
	bool success{};
	const char *group_name{ group.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();

		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_CONFIG)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->config_file_path = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", CONFIG_GROUP_SINK_MSG_CONV_CONFIG.data(), group_name);
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE)
		{
			config->conv_payload_type = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB)
		{
			auto temp = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, temp, config->conv_msg2p_lib))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB.data(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_COMP_ID)
		{
			config->conv_comp_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->debug_payload_dir = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->debug_payload_dir))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in '%s'", CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR.data(), group_name);
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS)
		{
			config->multiple_payloads = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API)
		{
			config->conv_msg2p_new_api = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL)
		{
			config->conv_frame_interval = itr->second.as<uint>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_msgconsumer(MsgConsumerConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group, nullptr, &error) };
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_CONFIG)
		{
			config->config_file_path = get_absolute_file_path(
					m_file_path, glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_MSG_CONSUMER_CONFIG, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_PROTO_LIB)
		{
			config->proto_lib = glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_MSG_CONSUMER_PROTO_LIB, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_CONN_STR)
		{
			config->conn_str = glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_MSG_CONSUMER_CONN_STR, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_SENSOR_LIST_FILE)
		{
			config->sensor_list_file = get_absolute_file_path(
					m_file_path,
					glib::key_file_get_string(m_key_file, group, CONFIG_GROUP_MSG_CONSUMER_SENSOR_LIST_FILE, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_SUBSCRIBE_TOPIC_LIST)
		{
			gsize length;
			std::vector<std::string> topic_list{ glib::key_file_get_string_list(
					m_key_file, group, CONFIG_GROUP_MSG_CONSUMER_SUBSCRIBE_TOPIC_LIST, &length, &error) };

			CHECK_ERROR(error)
			if(length < 1)
			{
				TADS_ERR_MSG_V("%s at least one topic must be provided", __func__);
				goto done;
			}
			config->subscribe_topic_list = topic_list;
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_msgconsumer_yaml(MsgConsumerConfig *config, std::string_view group)
{
	bool success{};
	const char *group_name{ group.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_CONFIG)
		{
			config->config_file_path = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_PROTO_LIB)
		{
			config->proto_lib = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_CONN_STR)
		{
			config->conn_str = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_SENSOR_LIST_FILE)
		{
			auto temp = itr->second.as<std::string>();
			char *str = new char[G_PATH_MAX];
			std::strncpy(str, temp.c_str(), G_PATH_MAX - 1);
			config->sensor_list_file = new char[G_PATH_MAX];
			if(!get_absolute_file_path_yaml(m_file_path, str, config->sensor_list_file))
			{
				TADS_ERR_MSG_V("Could not parse labels file path");
				delete[] str;
				goto done;
			}
			delete[] str;
		}
		else if(key == CONFIG_GROUP_MSG_CONSUMER_SUBSCRIBE_TOPIC_LIST)
		{
			char **topic_list_;
			auto temp = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(temp);
			int length = vec.size();
			topic_list_ = new char *[length + 1];

			for(int i = 0; i < length; i++)
			{
				char *str2 = new char[G_PATH_MAX];
				std::strncpy(str2, vec[i].c_str(), G_PATH_MAX - 1);
				topic_list_[i] = str2;
			}
			topic_list_[length] = nullptr;

			if(length < 1)
			{
				TADS_ERR_MSG_V("%s at least one topic must be provided", __func__);
				goto done;
			}

			config->subscribe_topic_list.clear();
			for(int i = 0; i < length; i++)
			{
				config->subscribe_topic_list.emplace_back(topic_list_[i]);
			}
			g_strfreev(topic_list_);
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}
	success = true;

done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_osd(OSDConfig *config)
{
	bool success{};
	GError *error{};
	auto group_name = CONFIG_GROUP_OSD;

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group_name, nullptr, &error) };
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group_name.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable);
#endif
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->gpu_id);
#endif
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			uint nvbuf_memory_type = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)

			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), nvbuf_memory_type);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_MODE)
		{
			int mode = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)

			config->mode = static_cast<NvOSD_Mode>(mode);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), mode);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_BORDER_WIDTH)
		{
			config->border_width = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->border_width);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_BORDER_COLOR)
		{
			config->border_color = glib::key_file_get_string(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->border_color.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_SIZE)
		{
			config->text_size = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->text_size);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_COLOR)
		{
			gsize length;
			double *list = glib::key_file_get_double_list(m_key_file, group_name, key, &length, &error);
			CHECK_ERROR(error)
			if(length != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			config->text_color.red = list[0];
			config->text_color.green = list[1];
			config->text_color.blue = list[2];
			config->text_color.alpha = list[3];
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=#%lu'", key.data(), length);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_BG_COLOR)
		{
			gsize length = 0;
			double *list = glib::key_file_get_double_list(m_key_file, group_name, key, &length, &error);
			CHECK_ERROR(error)
			if(length != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			config->text_bg_color.red = list[0];
			config->text_bg_color.green = list[1];
			config->text_bg_color.blue = list[2];
			config->text_bg_color.alpha = list[3];

			if(config->text_bg_color.red > 0 || config->text_bg_color.green > 0 || config->text_bg_color.blue > 0 ||
				 config->text_bg_color.alpha > 0)
				config->text_has_bg = true;
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=#%lu'", key.data(), length);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_FONT)
		{
			config->font = glib::key_file_get_string(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->font.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_ENABLE)
		{
			config->enable_clock = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable_clock);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_X_OFFSET)
		{
			config->clock_x_offset = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->clock_x_offset);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_Y_OFFSET)
		{
			config->clock_y_offset = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->clock_y_offset);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE)
		{
			config->clock_text_size = glib::key_file_get_integer(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->clock_text_size);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_HW_BLEND_COLOR_ATTR)
		{
			config->hw_blend_color_attr = glib::key_file_get_string(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->hw_blend_color_attr.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_COLOR)
		{
			gsize length;
			double *list = glib::key_file_get_double_list(m_key_file, group_name, key, &length, &error);
			CHECK_ERROR(error)
			if(length != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			config->clock_color.red = list[0];
			config->clock_color.green = list[1];
			config->clock_color.blue = list[2];
			config->clock_color.alpha = list[3];
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=#%lu'", key.data(), length);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_TEXT)
		{
			config->display_text = glib::key_file_get_boolean(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->display_text);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_BBOX)
		{
			config->display_bbox = glib::key_file_get_boolean(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->display_bbox);
#endif
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_MASK)
		{
			config->display_mask = glib::key_file_get_boolean(m_key_file, group_name, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->display_mask);
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group_name.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_osd_yaml(OSDConfig *config)
{
	bool success{};

	const char *group_name{ CONFIG_GROUP_OSD.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();

		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_OSD_MODE)
		{
			config->mode = static_cast<NvOSD_Mode>(itr->second.as<int>());
		}
		else if(key == CONFIG_GROUP_OSD_BORDER_WIDTH)
		{
			config->border_width = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_OSD_BORDER_COLOR)
		{
			config->border_color = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_SIZE)
		{
			config->text_size = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_COLOR)
		{
			auto str = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(str);
			if(vec.size() != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			std::vector<int> temp;
			for(int i = 0; i < 4; i++)
			{
				int temp1 = std::stoi(vec[i]);
				temp.push_back(temp1);
			}
			config->text_color.red = temp[0];
			config->text_color.green = temp[1];
			config->text_color.blue = temp[2];
			config->text_color.alpha = temp[3];
		}
		else if(key == CONFIG_GROUP_OSD_TEXT_BG_COLOR)
		{
			auto str = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(str);
			if(vec.size() != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			std::vector<int> temp;
			for(int i = 0; i < 4; i++)
			{
				int temp1 = std::stoi(vec[i]);
				temp.push_back(temp1);
			}
			config->text_bg_color.red = temp[0];
			config->text_bg_color.green = temp[1];
			config->text_bg_color.blue = temp[2];
			config->text_bg_color.alpha = temp[3];

			if(config->text_bg_color.red > 0 || config->text_bg_color.green > 0 || config->text_bg_color.blue > 0 ||
				 config->text_bg_color.alpha > 0)
				config->text_has_bg = true;
		}
		else if(key == CONFIG_GROUP_OSD_FONT)
		{
			config->font = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_ENABLE)
		{
			config->enable_clock = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_X_OFFSET)
		{
			config->clock_x_offset = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_Y_OFFSET)
		{
			config->clock_y_offset = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE)
		{
			config->clock_text_size = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_OSD_HW_BLEND_COLOR_ATTR)
		{
			config->hw_blend_color_attr = itr->second.as<std::string>();
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(itr->second.as<uint>());
		}
		else if(key == CONFIG_GROUP_OSD_CLOCK_COLOR)
		{
			auto str = itr->second.as<std::string>();
			std::vector<std::string> vec = split_string(str);
			if(vec.size() != 4)
			{
				TADS_ERR_MSG_V("Color params should be exactly 4 floats {r, g, b, a} between 0 and 1");
				goto done;
			}
			std::vector<int> temp;
			for(int i = 0; i < 4; i++)
			{
				int temp1 = std::stoi(vec[i]);
				temp.push_back(temp1);
			}
			config->clock_color.red = temp.at(0);
			config->clock_color.green = temp.at(1);
			config->clock_color.blue = temp.at(2);
			config->clock_color.alpha = temp.at(3);
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_TEXT)
		{
			config->display_text = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_BBOX)
		{
			config->display_bbox = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_OSD_SHOW_MASK)
		{
			config->display_mask = itr->second.as<bool>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_sink(SinkSubBinConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	if(!glib::key_file_get_integer(m_key_file, group, CONFIG_KEY_ENABLE, &error) || error != nullptr)
		return true;

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group, nullptr, &error) };
	CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
	TADS_DBG_MSG_V("parsing configuration group '%s'", group.data());
#endif

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->enable);
#endif
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->encoder_config.gpu_id = config->render_config.gpu_id =
					glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.gpu_id);
#endif
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			uint nvbuf_memory_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

			config->render_config.nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), nvbuf_memory_type);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_TYPE)
		{
			int sink_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			config->type = static_cast<SinkType>(sink_type);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), sink_type);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_ONLY_FOR_DEMUX)
		{
			config->link_to_demux = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->link_to_demux);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_WIDTH)
		{
			int width = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

			config->render_config.width = width;
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), width);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_HEIGHT)
		{
			int height = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->render_config.height = height;
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), height);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_QOS)
		{
			config->render_config.qos = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->render_config.qos);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_SYNC)
		{
			config->sync = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->sync);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_CONTAINER)
		{
			int container = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
			config->encoder_config.container = static_cast<ContainerType>(container);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), container);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_CODEC)
		{
			int codec = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

			config->encoder_config.codec = static_cast<EncoderCodecType>(codec);
#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), codec);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_ENC_TYPE)
		{
			int enc_type = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->encoder_config.enc_type = static_cast<EncoderEngineType>(enc_type);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), enc_type);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_COPY_META)
		{
			config->encoder_config.copy_meta = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.copy_meta);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_OUTPUT_IO_MODE)
		{
			int output_io_mode = glib::key_file_get_integer(m_key_file, group, key, &error);
			config->encoder_config.output_io_mode = static_cast<EncOutputIOMode>(output_io_mode);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), output_io_mode);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_SW_PRESET)
		{
			config->encoder_config.sw_preset = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.sw_preset);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_BITRATE)
		{
			config->encoder_config.bitrate = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.bitrate);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_PROFILE)
		{
			config->encoder_config.profile = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.profile);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_IFRAMEINTERVAL)
		{
			config->encoder_config.iframeinterval = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.iframeinterval);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_OUTPUT_FILE)
		{
			config->encoder_config.output_file = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->encoder_config.output_file.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_OUTPUT_FILE_PATH)
		{
			config->encoder_config.output_file_path = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->encoder_config.output_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_SOURCE_ID)
		{
			config->source_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->source_id);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_RTSP_PORT)
		{
			config->encoder_config.rtsp_port = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.rtsp_port);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_UDP_PORT)
		{
			config->encoder_config.udp_port = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->encoder_config.udp_port);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_UDP_BUFFER_SIZE)
		{
			config->encoder_config.udp_buffer_size = glib::key_file_get_uint64(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%lu'", key.data(), config->encoder_config.udp_buffer_size);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_COLOR_RANGE)
		{
			config->render_config.color_range = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->render_config.color_range);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_CONN_ID)
		{
			config->render_config.conn_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->render_config.conn_id);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_PLANE_ID)
		{
			config->render_config.plane_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->render_config.plane_id);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_SET_MODE)
		{
			config->render_config.set_mode = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->render_config.set_mode);
#endif
		}
		else if((key == CONFIG_GROUP_SINK_MSG_CONV_CONFIG) || (key == CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE) ||
						(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB) || (key == CONFIG_GROUP_SINK_MSG_CONV_COMP_ID) ||
						(key == CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR) ||
						(key == CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS) ||
						(key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API) || (key == CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL))
		{
			if(!parse_msgconv(&config->msg_conv_broker_config, group))
				goto done;
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB)
		{
			config->msg_conv_broker_config.proto_lib = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->msg_conv_broker_config.proto_lib.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR)
		{
			config->msg_conv_broker_config.conn_str = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->msg_conv_broker_config.conn_str.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_TOPIC)
		{
			config->msg_conv_broker_config.topic = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->msg_conv_broker_config.topic.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE)
		{
			config->msg_conv_broker_config.broker_config_file_path =
					get_absolute_file_path(m_file_path, glib::key_file_get_string(m_key_file, group, key, &error));
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%s'", key.data(), config->msg_conv_broker_config.broker_config_file_path.c_str());
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID)
		{
			config->msg_conv_broker_config.broker_comp_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->msg_conv_broker_config.broker_comp_id);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_DISABLE_MSG_CONVERTER)
		{
			config->msg_conv_broker_config.disable_msgconv = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->msg_conv_broker_config.disable_msgconv);
#endif
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_NEW_API)
		{
			config->msg_conv_broker_config.new_api = glib::key_file_get_boolean(m_key_file, group, key, &error);
			CHECK_ERROR(error)

#ifdef TADS_CONFIG_PARSER_DEBUG
			TADS_DBG_MSG_V("set config '%s=%d'", key.data(), config->msg_conv_broker_config.new_api);
#endif
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_sink_yaml(SinkSubBinConfig *config, std::string_view group)
{
	bool success{};
	const char *group_name{ group.data() };
	auto node = m_file_yml[group_name];

	if(node[CONFIG_KEY_ENABLE.data()])
	{
		auto enable = node[CONFIG_KEY_ENABLE.data()].as<bool>();
		if(!enable)
		{
			success = true;
			goto done;
		}
	}

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();

		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->encoder_config.gpu_id = config->render_config.gpu_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			uint nvbuf_memory_type = itr->second.as<uint>();
			config->render_config.nvbuf_memory_type = static_cast<NvBufMemoryType>(nvbuf_memory_type);
		}
		else if(key == CONFIG_GROUP_SINK_TYPE)
		{
			config->type = (SinkType)itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_ONLY_FOR_DEMUX)
		{
			config->link_to_demux = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_WIDTH)
		{
			config->render_config.width = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_HEIGHT)
		{
			config->render_config.height = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_QOS)
		{
			config->render_config.qos = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_SYNC)
		{
			config->sync = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_CONTAINER)
		{
			config->encoder_config.container = (ContainerType)itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_CODEC)
		{
			config->encoder_config.codec = (EncoderCodecType)itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_ENC_TYPE)
		{
			config->encoder_config.enc_type = (EncoderEngineType)itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_BITRATE)
		{
			config->encoder_config.bitrate = itr->second.as<int>();
		}
		else if(key == CONFIG_GROUP_SINK_PROFILE)
		{
			config->encoder_config.profile = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_IFRAMEINTERVAL)
		{
			config->encoder_config.iframeinterval = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_OUTPUT_FILE)
		{
			config->encoder_config.output_file = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_SINK_OUTPUT_FILE_PATH)
		{
			config->encoder_config.output_file_path = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_SINK_SOURCE_ID)
		{
			config->source_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_RTSP_PORT)
		{
			config->encoder_config.rtsp_port = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_UDP_PORT)
		{
			config->encoder_config.udp_port = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_UDP_BUFFER_SIZE)
		{
			config->encoder_config.udp_buffer_size = itr->second.as<uint64_t>();
		}
		else if(key == CONFIG_GROUP_SINK_COLOR_RANGE)
		{
			config->render_config.color_range = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_CONN_ID)
		{
			config->render_config.conn_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_PLANE_ID)
		{
			config->render_config.plane_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_SET_MODE)
		{
			config->render_config.set_mode = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_CONV_CONFIG || key == CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE ||
						key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB || key == CONFIG_GROUP_SINK_MSG_CONV_COMP_ID ||
						key == CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR ||
						key == CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS || key == CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API ||
						key == CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL)
		{
			success = parse_msgconv_yaml(&config->msg_conv_broker_config, group);
			if(!success)
				goto done;
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB)
		{
			config->msg_conv_broker_config.proto_lib = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR)
		{
			config->msg_conv_broker_config.conn_str = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_TOPIC)
		{
			config->msg_conv_broker_config.topic = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE)
		{
			auto temp = itr->second.as<std::string>();
			if(!get_absolute_file_path_yaml(m_file_path, temp, config->msg_conv_broker_config.broker_config_file_path))
			{
				TADS_ERR_MSG_V("Could not parse '%s' in group '%s'", key.c_str(), group_name);
				goto done;
			}
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID)
		{
			config->msg_conv_broker_config.broker_comp_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_DISABLE_MSG_CONVERTER)
		{
			config->msg_conv_broker_config.disable_msgconv = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_SINK_MSG_BROKER_NEW_API)
		{
			config->msg_conv_broker_config.new_api = itr->second.as<bool>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.c_str(), group_name);
		}
	}

	success = true;
done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_tiled_display(TiledDisplayConfig *config)
{
	bool success{};
	GError *error{};
	std::string_view group_name{ CONFIG_GROUP_TILED_DISPLAY };

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group_name, nullptr, &error) };
	CHECK_ERROR(error)
	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable =
					static_cast<TiledDisplayState>(glib::key_file_get_integer(m_key_file, group_name, CONFIG_KEY_ENABLE, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group_name, CONFIG_KEY_GPU_ID, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(
					glib::key_file_get_integer(m_key_file, group_name, CONFIG_KEY_CUDA_MEMORY_TYPE, &error));
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_DISPLAY_ROWS)
		{
			config->rows = glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_DISPLAY_ROWS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_DISPLAY_COLUMNS)
		{
			config->columns = glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_DISPLAY_COLUMNS, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_DISPLAY_WIDTH)
		{
			config->width = glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_DISPLAY_WIDTH, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_DISPLAY_HEIGHT)
		{
			config->height = glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_DISPLAY_HEIGHT, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_COMPUTE_HW)
		{
			config->compute_hw = glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_COMPUTE_HW, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_TILED_DISPLAY_BUFFER_POOL_SIZE)
		{
			config->buffer_pool_size =
					glib::key_file_get_integer(m_key_file, group_name, CONFIG_GROUP_TILED_DISPLAY_BUFFER_POOL_SIZE, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group_name.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_tiled_display_yaml(TiledDisplayConfig *config)
{
	bool success{};
	const char *group_name{ CONFIG_GROUP_TILED_DISPLAY.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto param_key = itr->first.as<std::string>();
		if(param_key == CONFIG_KEY_ENABLE)
		{
			config->enable = static_cast<TiledDisplayState>(itr->second.as<int>());
		}
		else if(param_key == CONFIG_GROUP_TILED_DISPLAY_ROWS)
		{
			config->rows = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_GROUP_TILED_DISPLAY_COLUMNS)
		{
			config->columns = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_GROUP_TILED_DISPLAY_WIDTH)
		{
			config->width = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_GROUP_TILED_DISPLAY_HEIGHT)
		{
			config->height = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_KEY_CUDA_MEMORY_TYPE)
		{
			config->nvbuf_memory_type = static_cast<NvBufMemoryType>(itr->second.as<uint>());
		}
		else if(param_key == CONFIG_GROUP_TILED_COMPUTE_HW)
		{
			config->compute_hw = itr->second.as<uint>();
		}
		else if(param_key == CONFIG_GROUP_TILED_DISPLAY_BUFFER_POOL_SIZE)
		{
			config->buffer_pool_size = itr->second.as<uint>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", param_key.c_str(), group_name);
			goto done;
		}
	}
	success = true;

done:

	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_image_save(ImageSaveConfig *config, std::string_view group)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys{ glib::key_file_get_keys(m_key_file, group, nullptr, &error) };
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_OUTPUT_FOLDER_PATH)
		{
			std::string output_folder_path = glib::key_file_get_string(m_key_file, group, key, &error);
			config->output_folder_path = get_absolute_file_path(m_file_path, output_folder_path);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_CSV_TIME_RULES_PATH)
		{
			config->frame_to_skip_rules_path = glib::key_file_get_string(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_FULL_FRAME_IMG_SAVE)
		{
			config->save_image_full_frame = glib::key_file_get_double(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_CROPPED_OBJECT_IMG_SAVE)
		{
			config->save_image_cropped_object = glib::key_file_get_double(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_SECOND_TO_SKIP_INTERVAL)
		{
			config->second_to_skip_interval = glib::key_file_get_double(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_QUALITY)
		{
			config->quality = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_CONFIDENCE)
		{
			config->min_confidence = glib::key_file_get_double(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MAX_CONFIDENCE)
		{
			config->max_confidence = glib::key_file_get_double(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_BOX_WIDTH)
		{
			config->min_box_width = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_BOX_HEIGHT)
		{
			config->min_box_height = glib::key_file_get_integer(m_key_file, group, key, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), group.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_image_save_yaml(ImageSaveConfig *config)
{
	bool success{ true };
	const char *group_name{ CONFIG_GROUP_IMG_SAVE.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_KEY_ENABLE)
		{
			config->enable = itr->second.as<bool>();
		}
		else if(key == CONFIG_KEY_GPU_ID)
		{
			config->gpu_id = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_OUTPUT_FOLDER_PATH)
		{
			config->output_folder_path = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_CSV_TIME_RULES_PATH)
		{
			config->frame_to_skip_rules_path = itr->second.as<std::string>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_FULL_FRAME_IMG_SAVE)
		{
			config->save_image_full_frame = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_CROPPED_OBJECT_IMG_SAVE)
		{
			config->save_image_cropped_object = itr->second.as<bool>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_SECOND_TO_SKIP_INTERVAL)
		{
			config->second_to_skip_interval = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_QUALITY)
		{
			config->quality = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_CONFIDENCE)
		{
			config->min_confidence = itr->second.as<double>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MAX_CONFIDENCE)
		{
			config->max_confidence = itr->second.as<double>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_BOX_WIDTH)
		{
			config->min_box_width = itr->second.as<uint>();
		}
		else if(key == CONFIG_GROUP_IMG_SAVE_MIN_BOX_HEIGHT)
		{
			config->min_box_height = itr->second.as<uint>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown param '%s' found in group '%s'", key.c_str(), group_name);
		}
	}

	return success;
}

bool ConfigParser::parse_tests(AppConfig *config)
{
	bool success{};
	GError *error{};

	std::vector<std::string> keys = glib::key_file_get_keys(m_key_file, CONFIG_GROUP_TESTS, nullptr, &error);
	CHECK_ERROR(error)

	for(std::string_view key : keys)
	{
		if(key == CONFIG_GROUP_TESTS_PIPELINE_RECREATE_SEC)
		{
			config->pipeline_recreate_sec =
					glib::key_file_get_integer(m_key_file, CONFIG_GROUP_TESTS, CONFIG_GROUP_TESTS_PIPELINE_RECREATE_SEC, &error);
			CHECK_ERROR(error)
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.data(), CONFIG_GROUP_TESTS.data());
		}
	}

	success = true;
done:
	if(error)
	{
		g_error_free(error);
	}
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}

bool ConfigParser::parse_tests_yaml(AppConfig *config)
{
	bool success{ true };
	const char *group_name{ CONFIG_GROUP_TESTS.data() };
	auto node = m_file_yml[group_name];

	for(YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
	{
		auto key = itr->first.as<std::string>();
		if(key == CONFIG_GROUP_TESTS_PIPELINE_RECREATE_SEC)
		{
			config->pipeline_recreate_sec = itr->second.as<uint>();
		}
		else
		{
			TADS_WARN_MSG_V("Unknown key '%s' for group '%s'", key.c_str(), group_name);
		}
	}
	return success;
}