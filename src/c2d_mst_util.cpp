#include <cstdio>
#include <cstring>
#include <ctime>
#include <json-glib/json-glib.h>

#include "c2d_msg_util.hpp"

/**
 * This function assumes the UTC time and string in the following format.
 * "2024-03-05T20:02:00.051Z" - Milliseconds are optional.
 */
static time_t nvds_c2d_str_to_second(const char *str)
{
	char *err;
	struct tm tm_log = { 0 };
	time_t t1;

	g_return_val_if_fail(str, -1);

	err = strptime(str, "%Y-%m-%dT%H:%M:%S", &tm_log);
	if(err == nullptr)
	{
		TADS_ERR_MSG_V("Error in parsing time string");
		return -1;
	}

	t1 = mktime(&tm_log);
	return t1;
}

static time_t nvds_get_current_utc_time()
{
	struct timespec ts;
	time_t tloc, t1;
	struct tm tm_log;

	clock_gettime(CLOCK_REALTIME, &ts);

	memcpy(&tloc, (void *)(&ts.tv_sec), sizeof(time_t));
	gmtime_r(&tloc, &tm_log);
	t1 = mktime(&tm_log);
	return t1;
}

NvDsC2DMsg *nvds_c2d_parse_cloud_message(void *data, uint size)
{
	int start, duration;
	bool start_rec, success;
	JsonNode *root_node;
	NvDsC2DMsg *msg;
	GError *error{};
	char *sensor_str{};

	/**
	 * Following minimum json message is expected to trigger the start / stop
	 * of smart record.
	 * {
	 *   command: string   // <start-recording / stop-recording>
	 *   start: string     // "2020-05-18T20:02:00.051Z"
	 *   end: string       // "2020-05-18T20:02:02.851Z",
	 *   sensor: {
	 *     id: string
	 *   }
	 * }
	 */
	auto *sr_msg = new NvDsC2DMsgSR;
	JsonParser *parser = json_parser_new();
	success = json_parser_load_from_data(parser, reinterpret_cast<const char *>(data), size, &error);
	if(!success)
	{
		TADS_ERR_MSG_V("Error in parsing json message %s", error->message);
		g_error_free(error);
		g_object_unref(parser);
		return nullptr;
	}

	root_node = json_parser_get_root(parser);
	if(JSON_NODE_HOLDS_OBJECT(root_node))
	{
		JsonObject *object;

		object = json_node_get_object(root_node);
		if(json_object_has_member(object, "command"))
		{
			const char *type = json_object_get_string_member(object, "command");
			if(!g_strcmp0(type, "start-recording"))
				start_rec = true;
			else if(!g_strcmp0(type, "stop-recording"))
				start_rec = false;
			else
			{
				TADS_WARN_MSG_V("wrong command %s", type);
				goto error;
			}
		}
		else
		{
			// 'command' field not provided, assume it to be start-recording.
			start_rec = true;
		}

		if(json_object_has_member(object, "sensor"))
		{
			JsonObject *json_object = json_object_get_object_member(object, "sensor");
			if(json_object_has_member(json_object, "id"))
			{
				sensor_str = g_strdup(json_object_get_string_member(json_object, "id"));
				if(!sensor_str)
				{
					TADS_WARN_MSG_V("wrong sensor.id value");
					goto error;
				}

				g_strstrip(sensor_str);
				if(!g_strcmp0(sensor_str, ""))
				{
					TADS_WARN_MSG_V("empty sensor.id value");
					goto error;
				}
			}
			else
			{
				TADS_WARN_MSG_V("wrong message format, missing 'sensor.id' field.");
				goto error;
			}
		}
		else
		{
			TADS_WARN_MSG_V("wrong message format, missing 'sensor.id' field.");
			goto error;
		}

		if(start_rec)
		{
			time_t start_utc, end_utc, cur_utc;
			const char *time_str;
			if(json_object_has_member(object, "start"))
			{
				time_str = json_object_get_string_member(object, "start");
				start_utc = nvds_c2d_str_to_second(time_str);
				if(start_utc < 0)
				{
					TADS_WARN_MSG_V("Error in parsing 'start' time - %s", time_str);
					goto error;
				}
				cur_utc = nvds_get_current_utc_time();
				start = cur_utc - start_utc;
				if(start < 0)
				{
					start = 0;
					TADS_WARN_MSG_V("start time is in future, setting it to current time");
				}
			}
			else
			{
				TADS_WARN_MSG_V("wrong message format, missing 'start' field.");
				goto error;
			}
			if(json_object_has_member(object, "end"))
			{
				time_str = json_object_get_string_member(object, "end");
				end_utc = nvds_c2d_str_to_second(time_str);
				if(end_utc < 0)
				{
					TADS_WARN_MSG_V("Error in parsing 'end' time - %s", time_str);
					goto error;
				}
				duration = end_utc - start_utc;
				if(duration < 0)
				{
					TADS_WARN_MSG_V("Negative duration (%d), setting it to zero", duration);
					duration = 0;
				}
			}
			else
			{
				// Duration is not specified that means stop event will be received later.
				duration = 0;
			}
		}
	}
	else
	{
		TADS_WARN_MSG_V("wrong message format - no json object");
		goto error;
	}

	sr_msg->sensor_str = sensor_str;
	if(start_rec)
	{
		sr_msg->start_time = start;
		sr_msg->duration = duration;
	}

	msg = new NvDsC2DMsg;
	if(start_rec)
		msg->type = NvDsC2DMsgType::SR_START;
	else
		msg->type = NvDsC2DMsgType::SR_STOP;

	msg->message = (void *)sr_msg;
	msg->msg_size = sizeof(NvDsC2DMsgSR);

	g_object_unref(parser);
	return msg;

error:
	g_object_unref(parser);
	g_free(sensor_str);
	g_free(sr_msg);
	return nullptr;
}

void nvds_c2d_release_message(NvDsC2DMsg *msg)
{
	if(msg->type == NvDsC2DMsgType::SR_STOP || msg->type == NvDsC2DMsgType::SR_START)
	{

		auto *sr_msg = reinterpret_cast<NvDsC2DMsgSR *>(msg->message);
		g_free(sr_msg->sensor_str);
	}
	g_free(msg->message);
	g_free(msg);
}

bool nvds_c2d_parse_sensor(C2DContext *ctx, std::string_view file)
{
	bool success{};
	GKeyFile *cfg_file;
	GError *error{};
	char** groups{};
	bool is_enabled;
	int sensor_id;
	char *sensor_str;

	GHashTable *hash_map{ ctx->hash_map };

	g_return_val_if_fail(hash_map, false);

	cfg_file = g_key_file_new();
	if(!g_key_file_load_from_file(cfg_file, file.data(), G_KEY_FILE_NONE, &error))
	{
		TADS_ERR_MSG_V("Failed to load file: %s", error->message);
		goto done;
	}

	groups = g_key_file_get_groups(cfg_file, nullptr);
	for(char** group = groups; group != nullptr; group++)
	{
		if(starts_with(*group, CONFIG_GROUP_SENSOR.data(), CONFIG_GROUP_SENSOR.length()))
		{
			if(sscanf(*group, "sensor%u", &sensor_id) < 1) // NOLINT(*-err34-c)
			{
				TADS_ERR_MSG_V("Wrong sensor group name %s", *group);
				goto done;
			}

			is_enabled = g_key_file_get_boolean(cfg_file, *group, CONFIG_KEY_ENABLE.data(), &error);
			if(!is_enabled)
			{
				// Not enabled, skip the parsing of source id.
				continue;
			}
			else
			{
				void *hash_val;
				sensor_str = g_key_file_get_string(cfg_file, *group, CONFIG_KEY_ID.data(), &error);
				if(error)
				{
					TADS_ERR_MSG_V("Error: %s", error->message);
					goto done;
				}

				hash_val = g_hash_table_lookup(hash_map, sensor_str);
				if(hash_val != nullptr)
				{
					TADS_ERR_MSG_V("Duplicate entries for key %s", sensor_str);
					goto done;
				}
				g_hash_table_insert(hash_map, sensor_str, &sensor_id);
			}
		}
	}

	success = true;

done:
	if(error)
	{
		g_error_free(error);
	}

	if(groups)
	{
		g_strfreev(groups);
	}

	if(cfg_file)
	{
		g_key_file_free(cfg_file);
	}

	return success;
}
