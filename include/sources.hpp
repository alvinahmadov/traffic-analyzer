#ifndef TADS_SOURCES_HPP
#define TADS_SOURCES_HPP

#include <sys/time.h>

#include <gst-nvdssr.h>

#include "common.hpp"

enum class SourceType
{
	CAMERA_V4L2 = 1,
	URI,
	URI_MULTIPLE [[maybe_unused]],
	RTSP,
	CAMERA_CSI
};

struct SourceConfig : BaseConfig
{
	SourceType type;
	bool enable;
	bool loop;
	bool live_source;
	bool intra_decode;
	bool low_latency_mode;
	uint smart_record;
	int source_width;
	int source_height;
	int source_fps_n;
	int source_fps_d;
	int camera_csi_sensor_id;
	int camera_v4l2_dev_node;
	std::string uri;
	std::string dir_path;
	std::string file_prefix;
	int latency{ 100 };
	uint smart_rec_cache_size;
	uint smart_rec_container;
	uint smart_rec_def_duration;
	uint smart_rec_duration;
	uint smart_rec_start_time;
	uint smart_rec_interval;
	[[maybe_unused]] uint num_sources;
	uint gpu_id;
	uint camera_id;
	[[maybe_unused]] uint source_id;
	uint select_rtp_protocol;
	uint num_decode_surfaces{ 16 };
	uint num_extra_surfaces{ 1 };
	NvBufMemoryType nvbuf_memory_type;
	uint cuda_memory_type;
	//DewarperConfig dewarper_config;
	uint drop_frame_interval;
	int rtsp_reconnect_interval_sec;
	int rtsp_reconnect_attempts;
	uint udp_buffer_size;
	/** Video format to be applied at nvvideoconvert source pad. */
	std::string video_format;
};

struct SourceParentBin;

struct SourceBin
{
	GstElement *bin;
	GstElement *src_elem;
	GstElement *cap_filter;
	GstElement *cap_filter1;
	GstElement *depay;
	GstElement *parser;
	[[maybe_unused]] GstElement *enc_que;
	GstElement *dec_que;
	GstElement *decodebin;
	[[maybe_unused]] GstElement *enc_filter;
	[[maybe_unused]] GstElement *encbin_que;
	GstElement *tee;
	GstElement *tee_rtsp_pre_decode;
	GstElement *tee_rtsp_post_decode;
	GstElement *fakesink_queue;
	GstElement *fakesink;
	GstElement *nvvidconv;

	[[maybe_unused]] bool do_record;
	[[maybe_unused]] uint64_t pre_event_rec;
	GMutex bin_lock;
	uint bin_id;
	int rtsp_reconnect_interval_sec;
	int rtsp_reconnect_attempts{ -1 };
	int num_rtsp_reconnects{ -1 };
	bool have_eos{};
	struct timeval last_buffer_time = {};
	struct timeval last_reconnect_time = {};
	gulong src_buffer_probe;
	gulong rtspsrc_monitor_probe;
	[[maybe_unused]] void *bbox_meta;
	[[maybe_unused]] GstBuffer *inbuf;
	[[maybe_unused]] char *location;
	[[maybe_unused]] char *file;
	[[maybe_unused]] char *direction;
	int latency;
	uint udp_buffer_size;
	[[maybe_unused]] bool got_key_frame;
	[[maybe_unused]] bool eos_done;
	[[maybe_unused]] bool reset_done;
	[[maybe_unused]] bool live_source;
	bool reconfiguring;
	bool async_state_watch_running;
	//DewarperBin dewarper_bin;
	[[maybe_unused]] gulong probe_id;
	uint64_t accumulated_base;
	uint64_t prev_accumulated_base;
	uint source_id;
	SourceConfig *config;
	SourceParentBin *parent_bin;
	NvDsSRContext *record_ctx;
};

struct SourceParentBin
{
	GstElement *bin;
	GstElement *streammux;
	GstElement *nvmultiurisrcbin;
	[[maybe_unused]] GThread *reset_thread{};
	std::vector<SourceBin> sub_bins{ MAX_SOURCE_BINS };
	uint num_bins;
	[[maybe_unused]] uint num_fr_on;
	[[maybe_unused]] bool live_source;
	gulong nvstreammux_eosmonitor_probe;
};

bool create_source_bin(SourceConfig *config, SourceBin *source_bin);

/**
 * Initialize @ref NvDsSrcParentBin. It creates and adds source and
 * other elements needed for processing to the source_parent.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_SOURCE
 *
 * @param[in] num_sub_bins number of source elements.
 * @param[in] configs array of pointers of type @ref NvDsSourceConfig
 *            parsed from configuration file.
 * @param[in] source_parent pointer to @ref NvDsSrcParentBin to be filled.
 *
 * @return true if source_parent created successfully.
 */
bool create_multi_source_bin(uint num_sub_bins, std::vector<SourceConfig> &configs, SourceParentBin *source_parent);

/**
 * Initialize @ref NvDsSrcParentBin. It creates and adds nvmultiurisrcbin
 * needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_SOURCE_LIST, @ref CONFIG_GROUP_SOURCE_ALL
 *
 * @param[in] num_sub_bins number of source elements.
 * @param[in] configs array of pointers of type @ref NvDsSourceConfig
 *            parsed from configuration file.
 * @param[in] source_parent pointer to @ref NvDsSrcParentBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_nvmultiurisrcbin_bin(uint num_sub_bins, const std::vector<SourceConfig> &configs,
																 SourceParentBin *source_parent);

bool reset_source_pipeline(void *data);
bool set_source_to_playing(void *data);
void *reset_encodebin(void *data);
void destroy_smart_record_bin(void *data);

#endif
