#ifndef TADS_C2D_MSG_UTIL_HPP
#define TADS_C2D_MSG_UTIL_HPP

#include "c2d_msg.hpp"

enum class NvDsC2DMsgType
{
	SR_START,
	SR_STOP
};

struct NvDsC2DMsg
{
	NvDsC2DMsgType type;
	void *message;
	[[maybe_unused]] uint msg_size;
};

struct NvDsC2DMsgSR
{
	char *sensor_str;
	int start_time;
	uint duration;
};

NvDsC2DMsg *nvds_c2d_parse_cloud_message(void *data, uint size);

void nvds_c2d_release_message(NvDsC2DMsg *msg);

bool nvds_c2d_parse_sensor(C2DContext *ctx, std::string_view file);

#endif // TADS_C2D_MSG_UTIL_HPP
