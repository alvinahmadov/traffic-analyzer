#ifndef TADS_ANALYTICS_HPP
#define TADS_ANALYTICS_HPP

#include <atomic>
#include <map>
#include <filesystem>
#include <utility>

#include <nvdscustomusermeta.h>

#include "common.hpp"

namespace fs = std::filesystem;

const size_t MAX_ANALYTICS_DATA_LENGTH { 60 };
const size_t MIN_ANALYTICS_DATA_LENGTH { 20 };

struct AppContext;

struct AnalyticsConfig : BaseConfig
{
	/**
	 * Enables or disables the plugin.
	 * */
	bool enable{};
	uint unique_id{ static_cast<uint>(-1) };
	/**
	 * Configuration file path for nvdsanalytics
	 * plugin.
	 * */
	std::string config_file_path{};
	std::string output_path{};
	int lp_min_length{ 6 };
	double lines_distance;
};

struct LineCrossingData
{
	inline static const std::string unknown_label{ "unknown" };
	std::atomic<bool> is_set{ false };
	std::string status;
	double timestamp;
	std::string time_str;

	LineCrossingData();
};

struct ClassifierData
{
	std::string label{ "unknown" };
	float confidence{ 0.0 };

	ClassifierData() = default;
	explicit ClassifierData(std::string label, float conf = 0.0): label(std::move(label)), confidence(conf)
	{}
};

struct TrafficAnalysisData
{
	inline static const std::string unknown_label{ "unknown" };
	inline static int distance {-1};

	uint64_t id;
	uint64_t index;
	std::string direction;
	LineCrossingData lc_top;
	LineCrossingData lc_bottom;
	ClassifierData classifier_data;
	std::vector<ClassifierData> lp_data;
	std::string output_path;
	bool has_image;

	TrafficAnalysisData();

	explicit TrafficAnalysisData(uint64_t id);

	[[maybe_unused]]
	void print_info() const;
	void save_to_file() const;

	[[nodiscard]]
	int get_object_speed() const;

	[[nodiscard]]
	std::string get_image_filename() const;

	[[nodiscard]]
	inline bool passed_lines() const
	{
		return this->lc_top.is_set && this->lc_bottom.is_set;
	}

	[[nodiscard]]
	inline bool ready() const
	{
		return passed_lines() && direction != unknown_label;
	}
};

struct AnalyticsBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *analytics_elem;

	std::map<uint64_t, TrafficAnalysisData> traffic_data_map;
	GDateTime *date_time = nullptr;
	GTimer *timer = nullptr;
};

/**
 * Function parses analytics metadata to do logic (PGIE, Tracker or
 * the last SGIE appearing in the pipeline)
 * */
void parse_analytics_metadata(AppContext *app_context, GstBuffer *buffer, NvDsBatchMeta *batch_meta);

// Function to create the bin and set properties
bool create_analytics_bin(AnalyticsConfig *config, AnalyticsBin *analytics);

#endif // TADS_ANALYTICS_HPP
