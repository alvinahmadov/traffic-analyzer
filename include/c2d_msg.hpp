#ifndef TADS_C2D_MSG_HPP
#define TADS_C2D_MSG_HPP

#include <nvmsgbroker.h>

#include "common.hpp"

struct MsgConsumerConfig : BaseConfig
{
	/**
	 * Enables or disables the message consumer.
	 * */
	bool enable;
	/**
	 * Path to the library having protocol adapter implementation.
	 *
	 * \example
	 * proto-lib=/opt/nvidia/deepstream/deepstream/lib/libnvds_kafka_proto.so
	 * */
	std::string proto_lib{};
	/**
	 * Connection string of the server.
	 *
	 * \example conn-str=foo.bar.com;80
	 * */
	std::string conn_str{};
	/**
	 * Path to the file having additional configurations
	 * for protocol adapter.
	 *
	 * \example config-file=../cfg_kafka.txt
	 * */
	std::string config_file_path{};
	/**
	 * List of topics to subscribe.
	 *
	 * \example subscribe-topic-list=toipc1;topic2;topic3
	 * */
	std::vector<std::string> subscribe_topic_list{};
	/**
	 * File having mappings from sensor instance_num to sensor name.
	 *
	 * \example
	 * sensor-list-file=dstest5_msgconv_sample_config.txt
	 * */
	std::string sensor_list_file{};
};

struct C2DContext
{
	[[maybe_unused]] void *lib_handle;
	std::string proto_lib;
	std::string conn_str;
	std::string config_file;
	void *data;
	GHashTable *hash_map;
	NvMsgBrokerClientHandle conn_handle;
	nv_msgbroker_subscribe_cb_t subscribe_cb;
};

using C2DContextPtr = std::unique_ptr<C2DContext>;
using C2DContextPtrRef = C2DContextPtr &;

void subscribe_cb(NvMsgBrokerErrorType flag, void *msg, int msg_len, char *topic, void *data);

C2DContextPtr
start_cloud_to_device_messaging(MsgConsumerConfig *config, nv_msgbroker_subscribe_cb_t subscribe_cb, void *data);
bool stop_cloud_to_device_messaging(C2DContextPtrRef context);

#endif // TADS_C2D_MSG_HPP
