#ifndef TADS_APP_HPP
#define TADS_APP_HPP

#include <cstdio>

#include <optional>

#include <gst-nvdscustommessage.h>
#include <gst-nvdscommonconfig.h>
#include <map>

#include "common.hpp"
#include "config.hpp"
#include "osd.hpp"
#include "perf.hpp"
#include "preprocess.hpp"
#include "primary_gie.hpp"
#include "sinks.hpp"
#include "sources.hpp"
#include "streammux.hpp"
#include "tiled_display.hpp"
#include "analytics.hpp"
#include "tracker.hpp"
#include "secondary_gie.hpp"
#include "secondary_preprocess.hpp"
#include "c2d_msg.hpp"
#include "image_save.hpp"

struct AppContext;

struct InstanceBin : BaseBin
{
	uint index;
	uint64_t all_bbox_buffer_probe_id;
	uint64_t primary_bbox_buffer_probe_id;
	[[maybe_unused]] uint64_t fps_buffer_probe_id;
	GstElement *bin;
	GstElement *tee;
	GstElement *msg_conv;

	PreProcessBin preprocess;
	PrimaryGieBin primary_gie;
	OSDBin osd;
	SecondaryGieBin secondary_gie;
	SecondaryPreProcessBin secondary_pre_process;
	TrackerBin tracker;
	SinkBin sink;
	SinkBin demux_sink;
	AnalyticsBin analytics;
	NvDsObjEncCtxHandle obj_enc_ctx_handle;
	AppContext *app_ctx;
};

struct Pipeline : BaseBin
{
	[[maybe_unused]] uint64_t primary_bbox_buffer_probe_id;
	[[maybe_unused]] uint bus_id;
	GstElement *pipeline;
	GstElement *tiler_tee;
	GstElement *demuxer;

	SourceParentBin multi_src_bin;
	std::vector<InstanceBin> instance_bins{ MAX_SOURCE_BINS };
	std::vector<InstanceBin> demux_instance_bins{ MAX_SOURCE_BINS };
	InstanceBin common_elements;
	TiledDisplayBin tiled_display;
};

struct AppConfig : BaseConfig
{
	bool enable_perf_measurement{};
	bool file_loop{};
	bool source_list_enabled{};
	uint pipeline_recreate_sec;
	size_t total_num_sources{};
	size_t num_source_sub_bins{};
	size_t num_secondary_gie_sub_bins{};
	size_t num_secondary_preprocess_sub_bins{};
	size_t num_sink_sub_bins{};
	size_t num_message_consumers;
	size_t sgie_batch_size;
	uint perf_measurement_interval_sec;
	std::string output_dir_path;
	std::string bbox_dir_path;
	std::string kitti_track_dir_path;
	std::string reid_track_dir_path;
	std::string terminated_track_output_path;
	std::string shadow_track_output_path;

	std::vector<std::string> uri_list;
	std::vector<std::string> sensor_id_list;
	std::vector<std::string> sensor_name_list;
	std::vector<SourceConfig> multi_source_configs{ MAX_SOURCE_BINS };
	StreammuxConfig streammux_config;
	OSDConfig osd_config;
	PreProcessConfig preprocess_config;
	std::vector<PreProcessConfig> secondary_preprocess_sub_bin_configs{ MAX_SECONDARY_PREPROCESS_BINS };
	GieConfig primary_gie_config;
	TrackerConfig tracker_config;
	std::vector<GieConfig> secondary_gie_sub_bin_configs{ MAX_SECONDARY_GIE_BINS };
	std::vector<SinkSubBinConfig> sink_bin_sub_bin_configs{ MAX_SINK_BINS };
	MsgConsumerConfig message_consumer_configs[MAX_MESSAGE_CONSUMERS];
	TiledDisplayConfig tiled_display_config;
	AnalyticsConfig analytics_config;
	SinkMsgConvBrokerConfig msg_conv_config;
	ImageSaveConfig image_save_config;

	/** To support nvmultiurisrcbin */
	bool use_nvmultiurisrcbin;
	bool stream_name_display;
	uint max_batch_size;
	std::string http_ip;
	std::string http_port;
	bool source_attr_all_parsed{};
	SourceConfig source_attr_all_config;

	/** To set Global GPU ID for all the componenents at once if needed
	 * This will be used in case gpu_id prop is not set for a component
	 * if gpu_id prop is set for a component, global_gpu_id will be overridden by it */
	int global_gpu_id{ -1 };
};

struct InstanceData
{
	gulong frame_num;
};

struct AppContext
{
	[[maybe_unused]] bool version{};
	[[maybe_unused]] bool cintr;
	bool show_bbox_text{};
	[[maybe_unused]] bool seeking{};
	bool quit{};
	int class_id{};
	int status{};
	uint instance_num{};
	int active_source_index{ -1 };

	GMutex app_lock;
	GCond app_cond;

	Pipeline pipeline;
	AppConfig config;
	std::array<InstanceData, MAX_SOURCE_BINS> instance_data{};
	std::array<C2DContextPtr, MAX_MESSAGE_CONSUMERS> c2d_contexts{};
	AppPerfStructInt perf_struct;
	NvDsFrameLatencyInfo *latency_info_array;
	GMutex latency_lock;

	/** Hash table to save NvDsSensorInfo
	 * obtained with REST API stream/add, remove operations
	 * The key is souce_id */
	GHashTable *sensor_info_hash;
	std::map<uint64_t, TrafficAnalysisData> traffic_data_map;

	/**
	 * @brief  Create DS Anyalytics Pipeline per the appCtx
	 *         configurations
	 * @param  bbox_generated_post_analytics_cb [IN] This callback
	 *         shall be triggered after analytics
	 *         (PGIE, Tracker or the last SGIE appearing
	 *         in the pipeline)
	 *         @see create_common_elements()
	 * @param  all_bbox_generated_cb [IN]
	 * @param  perf_cb [IN]
	 * @param  overlay_graphics_cb [IN]
	 */
	bool create_pipeline(perf_callback perf_cb = nullptr);
	bool pause_pipeline();
	bool resume_pipeline();
	void destroy_pipeline();

	/**
	 * Function to be called once all inferences (Primary + Secondary)
	 * are done. This is opportunity to modify content of the metadata.
	 * It should be modified according to network classes
	 * or can be removed altogether if not required.
	 */
	void all_bbox_generated(GstBuffer *buffer, NvDsBatchMeta *batch_meta);

	/**
	 * Function to add application specific metadata.
	 * Here it demonstrates how to display the URI of source in addition to
	 * the text generated after inference.
	 */
	bool overlay_graphics(GstBuffer *, NvDsBatchMeta *batch_meta, uint index);
private:
	/**
	 * Function to create common elements(Primary infer, tracker, secondary infer)
	 * of the pipeline. These components operate on muxed data from all the
	 * streams. So they are independent of number of streams in the pipeline.
	 */
	bool create_common_elements(GstElement **sink_elem, GstElement **src_elem);
	bool create_demux_pipeline(uint index = 0);

	/**
	 * Function to add components to pipeline which are dependent on number
	 * of streams. These components work on single buffer. If tiling is being
	 * used then single instance will be created otherwise < N > such instances
	 * will be created for < N > streams
	 */
	bool create_processing_instance(uint index = 0);
};

// bool seek_pipeline(AppCtx *app_ctx, glong milliseconds, bool seek_is_relative);

// void toggle_show_bbox_text(AppCtx *app_ctx);

// void restart_pipeline(AppCtx *app_ctx);

/**
 * Function to procure the NvDsSensorInfo for the source_id
 * that was added using the nvmultiurisrcbin REST API
 *
 * @param[in] app_ctx [IN/OUT] The application context
 *            providing the config info and where the
 *            pipeline resources are maintained
 * @param[in] source_id [IN] The unique source_id found in NvDsFrameMeta
 *
 * @return [transfer-floating] The NvDsSensorInfo for the source_id
 * that was added using the nvmultiurisrcbin REST API.
 * Please note that the returned pointer
 * will be valid only until the stream is removed.
 */
NvDsSensorInfo *get_sensor_info(AppContext *app_ctx, uint source_id);

#endif // TADS_APP_HPP
