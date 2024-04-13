#ifndef TADS_ANALYTICS_HPP
#define TADS_ANALYTICS_HPP

#include <atomic>
#include <map>
#include <filesystem>
#include <utility>

#include <nvdscustomusermeta.h>

#include "common.hpp"

namespace fs = std::filesystem;

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
	bool is_set;
	std::string status;
	double timestamp;
	std::string time_str;

	LineCrossingData();
};

using LineCrossingPair = std::pair<LineCrossingData, LineCrossingData>;

struct ClassifierData
{
	std::string label{ "unknown" };
	float confidence{ 0.0 };

	ClassifierData() = default;
	explicit ClassifierData(std::string label, float conf = 0.0);
};

struct TrafficAnalysisData
{
	inline static int distance{ -1 };

	uint64_t id;
	uint64_t index;
	std::string label;
	std::string direction;
	LineCrossingPair crossing_pair;
	ClassifierData classifier_data;
	std::vector<ClassifierData> lp_data;
	std::string output_path;
	bool has_image;

	TrafficAnalysisData();

	explicit TrafficAnalysisData(uint64_t id);
	TrafficAnalysisData(const TrafficAnalysisData &) = default;
	TrafficAnalysisData &operator=(const TrafficAnalysisData &) = default;

	[[maybe_unused]]
	void print_info() const;
	void save_to_file() const;

	[[nodiscard]]
	int get_object_speed() const;

	[[nodiscard]]
	std::string get_image_filename() const;

	[[nodiscard]]
	bool lines_passed() const;

	[[nodiscard]]
	bool is_ready() const;
};

using TrafficAnalysisDataPtr = std::shared_ptr<TrafficAnalysisData>;
using TrafficAnalysisDataMap = std::map<uint64_t, TrafficAnalysisDataPtr>;

struct AnalyticsBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *analytics_elem;

	TrafficAnalysisDataMap traffic_data_map;
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
