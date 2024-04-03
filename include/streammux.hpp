#ifndef TADS_STREAMMUX_HPP
#define TADS_STREAMMUX_HPP

#include "common.hpp"

struct StreammuxConfig : BaseConfig
{
	int width;
	int height;
	int buffer_pool_size;
	int batch_size;
	int batched_push_timeout{ -1 };
	int compute_hw;
	int num_surface_per_frame;
	[[maybe_unused]] int interpolation_method;
	uint64_t frame_duration{ static_cast<uint64_t>(-1) };
	uint gpu_id{};
	NvBufMemoryType nvbuf_memory_type{ NvBufMemoryType::DEFAULT };
	bool live_source;
	bool enable_padding;
	[[maybe_unused]] bool is_parsed{};
	bool attach_sys_ts_as_ntp{ true };
	std::string config_file_path;
	bool sync_inputs;
	uint64_t max_latency;
	bool frame_num_reset_on_eos;
	bool frame_num_reset_on_stream_reset;
	bool async_process{ true };
	bool no_pipeline_eos{};
	bool use_nvmultiurisrcbin{};
};

// Function to create the bin and set properties
bool set_streammux_properties(StreammuxConfig *config, GstElement *streammux);

#endif // TADS_STREAMMUX_HPP
