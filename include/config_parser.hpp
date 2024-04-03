#ifndef TADS_CONFIG_PARSER_HPP
#define TADS_CONFIG_PARSER_HPP

#include <yaml-cpp/yaml.h>

#include "sources.hpp"
#include "preprocess.hpp"
#include "primary_gie.hpp"
#include "secondary_gie.hpp"
#include "tiled_display.hpp"
#include "sinks.hpp"
#include "osd.hpp"
#include "analytics.hpp"
#include "streammux.hpp"
#include "tracker.hpp"
#include "c2d_msg.hpp"
#include "image_save.hpp"

enum class ConfigFileType
{
	NONE,
	INI,
	YAML
};

struct AppConfig;

class ConfigParser
{
public:
	explicit ConfigParser(std::string cfg_file_path);

	~ConfigParser();

	bool parse(AppConfig *config);

protected:
	/**
	 * Function to read properties from TXT or INI configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_ini(AppConfig *config);
	/**
	 * Function to read properties from YML configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_yaml(AppConfig *config);

private:
	bool parse_app(AppConfig *config);
	bool parse_app_yaml(AppConfig *config);

	/**
	 * Function to read properties of source element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsSourceConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_SOURCE
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_source(SourceConfig *config, std::string_view group);
	bool
	parse_source_yaml(SourceConfig *config, std::vector<std::string> headers, std::vector<std::string> source_values);

	bool parse_source_list(AppConfig *config);

	/**
	 * Function to read properties of streammux element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsStreammuxConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_streammux(StreammuxConfig *config);
	bool parse_streammux_yaml(StreammuxConfig *config);

	/**
	 * Function to read properties of infer element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsGieConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_PRIMARY_GIE and
	 *            @ref CONFIG_GROUP_SECONDARY_GIE
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_gie(GieConfig *config, std::string_view group);
	bool parse_gie_yaml(GieConfig *config, std::string_view group);

	/**
	 * Function to read properties of tracker element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsTrackerConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_tracker(TrackerConfig *config);
	bool parse_tracker_yaml(TrackerConfig *config);

	/**
	 * Function to read properties of nvdspreprocess element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsPreProcessConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_PREPROCESS and
	 *            @ref CONFIG_GROUP_SECONDARY_PREPROCESS
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_preprocess(PreProcessConfig *config, std::string_view group);
	bool parse_preprocess_yaml(PreProcessConfig *config);

	/**
	 * Function to read properties of dsanalytics element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsDsAnalyticsConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_analytics(AnalyticsConfig *config);
	bool parse_analytics_yaml(AnalyticsConfig *config);

	/**
	 * Function to read properties of message converter element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsSinkMsgConvBrokerConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_MSG_CONVERTER
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_msgconv(SinkMsgConvBrokerConfig *config, std::string_view group);
	bool parse_msgconv_yaml(SinkMsgConvBrokerConfig *config, std::string_view group);
	bool parse_msgconv_yaml(SinkMsgConvBrokerConfig *config);

	/**
	 * Function to read properties of message consumer element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsMsgConsumerConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_MSG_CONSUMER
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_msgconsumer(MsgConsumerConfig *config, std::string_view group);
	bool parse_msgconsumer_yaml(MsgConsumerConfig *config, std::string_view group);

	/**
	 * Function to read properties of OSD element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsOSDConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_osd(OSDConfig *config);
	bool parse_osd_yaml(OSDConfig *config);

	/**
	 * Function to read properties of sink element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsSinkSubBinConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_SINK
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_sink(SinkSubBinConfig *config, std::string_view group);
	bool parse_sink_yaml(SinkSubBinConfig *config, std::string_view group);

	/**
	 * Function to read properties of tiler element from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsTiledDisplayConfig
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_tiled_display(TiledDisplayConfig *config);
	bool parse_tiled_display_yaml(TiledDisplayConfig *config);

	/**
	 * Function to read properties of image save from configuration file.
	 *
	 * @param[in] config pointer to @ref NvDsMsgConsumerConfig
	 * @param[in] group name of property group @ref CONFIG_GROUP_MSG_CONSUMER
	 *
	 * @return true if parsed successfully.
	 */
	bool parse_image_save(ImageSaveConfig *config, std::string_view group);
	bool parse_image_save_yaml(ImageSaveConfig *config);

	bool parse_tests(AppConfig *config);
	bool parse_tests_yaml(AppConfig *config);

private:
	ConfigFileType m_file_type;
	GKeyFile *m_key_file;
	YAML::Node m_file_yml;
	std::string m_file_path;
};

#endif // TADS_CONFIG_PARSER_HPP
