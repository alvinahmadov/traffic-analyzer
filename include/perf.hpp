#ifndef TADS_PERF_HPP
#define TADS_PERF_HPP

#include "common.hpp"
#include <array>

struct FPSSensorInfo
{
	uint source_id;
	char const *uri;
	char const *sensor_id;
	char const *sensor_name;
};

struct AppSourceDetail
{
	uint source_id;
	[[maybe_unused]] char *stream_name;
};

struct AppPerfStruct
{
	std::array<double, MAX_SOURCE_BINS> fps;
	std::array<double, MAX_SOURCE_BINS> fps_avg;
	uint num_instances;
	AppSourceDetail source_detail[MAX_SOURCE_BINS];
	[[maybe_unused]] uint active_source_size;
	[[maybe_unused]] bool stream_name_display;
	[[maybe_unused]] bool use_nvmultiurisrcbin;
};

typedef void (*perf_callback)(void *ctx, AppPerfStruct *str);

struct InstancePerfStruct
{
	uint buffer_cnt;
	uint total_buffer_cnt;
	struct timeval total_fps_time = {};
	struct timeval start_fps_time = {};
	struct timeval last_fps_time = {};
	struct timeval last_sample_fps_time = {};
};

struct AppPerfStructInt
{
	gulong measurement_interval_ms;
	gulong perf_measurement_timeout_id;
	uint num_instances;
	bool stop;
	void *context;
	GMutex struct_lock;
	perf_callback callback;
	[[maybe_unused]] GstPad *sink_bin_pad;
	[[maybe_unused]] gulong fps_measure_probe_id;
	InstancePerfStruct instance_str[MAX_SOURCE_BINS];
	uint dewarper_surfaces_per_frame;
	GHashTable *fps_info_hash;
	bool stream_name_display;
	bool use_nvmultiurisrcbin;
};

bool enable_perf_measurement(AppPerfStructInt *str, GstPad *sink_bin_pad, uint num_sources, gulong interval_sec,
														 /*uint num_surfaces_per_frame,*/ perf_callback callback);

void pause_perf_measurement(AppPerfStructInt *perf_struct);
void resume_perf_measurement(AppPerfStructInt *perf_struct);

#endif // TADS_PERF_HPP
