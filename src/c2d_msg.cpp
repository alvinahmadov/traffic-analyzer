#include <dlfcn.h>
#include <cstdlib>
#include <gst-nvdssr.h>

#include "c2d_msg.hpp"
#include "c2d_msg_util.hpp"
#include "sources.hpp"

static void connect_cb([[maybe_unused]] NvMsgBrokerClientHandle h_ptr, [[maybe_unused]] NvMsgBrokerErrorType status) {}

void subscribe_cb(NvMsgBrokerErrorType flag, void *msg, int msg_len, char *topic, void *data)
{
	NvDsC2DMsg *parsed_msg{};

	if(flag == NV_MSGBROKER_API_ERR)
	{
		TADS_ERR_MSG_V("Error in consuming message.");
	}
	else
	{
		GST_DEBUG("Consuming message, on topic[%s]. Payload =%.*s\n\n", topic, msg_len, (char *)msg);
	}

	if(data)
	{
		auto *c2d_context = reinterpret_cast<C2DContext *>(data);

		if(c2d_context->subscribe_cb)
		{
			return c2d_context->subscribe_cb(flag, msg, msg_len, topic, c2d_context->data);
		}
		else
		{
			auto *parent_bin = reinterpret_cast<SourceParentBin *>(c2d_context->data);
			if(!parent_bin)
			{
				TADS_WARN_MSG_V("Null user data");
				return;
			}
			parsed_msg = nvds_c2d_parse_cloud_message(msg, msg_len);
			if(parsed_msg == nullptr)
			{
				TADS_WARN_MSG_V("error in message parsing \n");
				return;
			}

			if(parsed_msg->type == NvDsC2DMsgType::SR_START || parsed_msg->type == NvDsC2DMsgType::SR_STOP)
			{

				NvDsSRSessionId session_id{};
				int sensor_id;
				NvDsSRContext *sr_context;
				auto *msg_sr = reinterpret_cast<NvDsC2DMsgSR *>(parsed_msg->message);

				if(c2d_context->hash_map)
				{
					void *key_val = g_hash_table_lookup(c2d_context->hash_map, msg_sr->sensor_str);
					if(key_val)
					{
						sensor_id = *(int *)key_val;
					}
					else
					{
						TADS_WARN_MSG_V("%s: Sensor id not found", msg_sr->sensor_str);
						goto error;
					}
				}
				else
				{
					sensor_id = std::strtol(msg_sr->sensor_str, nullptr, 10);
				}

				sr_context = parent_bin->sub_bins[sensor_id].record_ctx;
				if(!sr_context)
				{
					TADS_WARN_MSG_V("Null SR context handle.");
					goto error;
				}

				if(parsed_msg->type == NvDsC2DMsgType::SR_START)
				{
					NvDsSRStart(sr_context, &session_id, msg_sr->start_time, msg_sr->duration, nullptr);
				}
				else
				{
					NvDsSRStop(sr_context, session_id);
				}
			}
			else
			{
				TADS_WARN_MSG_V("Unknown message type.");
			}
		}
	}

error:
	if(parsed_msg)
	{
		nvds_c2d_release_message(parsed_msg);
	}
}

C2DContextPtr
start_cloud_to_device_messaging(MsgConsumerConfig *config, nv_msgbroker_subscribe_cb_t subscribe_cb, void *data)
{
	C2DContextPtr c2d_context;
	char **topic_list_{};
	int i, num_topic;

	if(config->conn_str.empty() || config->proto_lib.empty() || config->subscribe_topic_list.empty())
	{
		TADS_ERR_MSG_V("nullptr parameters");
		return nullptr;
	}

	c2d_context = std::make_unique<C2DContext>();
	c2d_context->proto_lib = config->proto_lib;
	c2d_context->conn_str = config->conn_str;
	c2d_context->config_file = config->config_file_path;

	if(subscribe_cb)
		c2d_context->subscribe_cb = subscribe_cb;

	if(data)
		c2d_context->data = data;

	if(!config->sensor_list_file.empty())
	{
		c2d_context->hash_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
		if(!nvds_c2d_parse_sensor(c2d_context.get(), config->sensor_list_file))
		{
			TADS_ERR_MSG_V("Failed to parse sensor list file");
			goto error;
		}
	}

	c2d_context->conn_handle = nv_msgbroker_connect(c2d_context->conn_str.data(), c2d_context->proto_lib.data(),
																									connect_cb, c2d_context->config_file.data());
	if(!c2d_context->conn_handle)
	{
		TADS_ERR_MSG_V("Failed to connect to broker.");
		goto error;
	}

	num_topic = config->subscribe_topic_list.size();
	topic_list_ = new char *[num_topic];
	for(i = 0; i < num_topic; i++)
	{
		topic_list_[i] = config->subscribe_topic_list.at(i).data();
	}

	if(nv_msgbroker_subscribe(c2d_context->conn_handle, topic_list_, num_topic, subscribe_cb, c2d_context.get()) !=
		 NV_MSGBROKER_API_OK)
	{
		TADS_ERR_MSG_V("Subscription to topic[s] failed\n");
		goto error;
	}

	delete[] topic_list_;

	return c2d_context;

error:
	if(c2d_context)
	{
		if(c2d_context->hash_map)
		{
			g_hash_table_unref(c2d_context->hash_map);
			c2d_context->hash_map = nullptr;
		}
		c2d_context.reset();
	}
	delete[] topic_list_;

	return c2d_context;
}

bool stop_cloud_to_device_messaging(C2DContextPtrRef context)
{
	NvMsgBrokerErrorType err;
	bool success{ true };

	g_return_val_if_fail(context, FALSE);

	err = nv_msgbroker_disconnect(context->conn_handle);
	if(err != NV_MSGBROKER_API_OK)
	{
		TADS_ERR_MSG_V("error(%d) in disconnect", err);
		success = false;
	}
	context->conn_handle = nullptr;

	if(context->hash_map)
	{
		g_hash_table_unref(context->hash_map);
		context->hash_map = nullptr;
	}
	context.reset();
	return success;
}
