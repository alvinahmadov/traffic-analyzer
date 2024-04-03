#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nvds_analytics_meta.h>

#include "analytics.hpp"
#include "image_save.hpp"
#include "app.hpp"

LineCrossingData::LineCrossingData():
	is_set{},
	status{ LineCrossingData::unknown_label },
	timestamp{}
{}

TrafficAnalysisData::TrafficAnalysisData():
	id{ static_cast<uint64_t>(-1) },
	crossing_entry{},
	crossing_exit{},
	lp_data{},
	direction{ TrafficAnalysisData::unknown_label }
{}

TrafficAnalysisData::TrafficAnalysisData(uint64_t obj_id):
	TrafficAnalysisData()
{
	id = obj_id;
}

[[maybe_unused]]
void TrafficAnalysisData::print_info() const
{
	if(!this->passed_lines())
		return;
	fmt::print("Номер         : {}\n", this->id);
	fmt::print("Класс         : {}\n", this->vehicle.label);
	for(const auto &lp : lp_data)
	{
		fmt::print("Госномер      : {} ({})\n", lp.label, lp.confidence);
	}
	fmt::print("Направление   : {}\n", this->direction);
	if(this->crossing_entry.is_set)
	{
		fmt::print("Вошел через   : {}\n", this->crossing_entry.status);
		fmt::print("Время         : {}\n", this->crossing_entry.timestamp);
	}
	if(this->crossing_exit.is_set)
	{
		fmt::print("Вышел через   : {}\n", this->crossing_exit.status);
		fmt::print("Время         : {}\n", this->crossing_exit.timestamp);
	}
	fmt::print("Скорость      : ~{} км/ч\n", (int)this->calculate_object_speed());
}

void TrafficAnalysisData::save_to_file(const std::string &output_path) const
{
	std::ostringstream output;
	std::ofstream file;
	GDateTime *date;
	char *date_string;

	if(output_path.empty())
		return;

	output << fmt::format("Номер         : {}\n", this->id);
	output << fmt::format("Класс         : {}\n", this->vehicle.label);

	output << fmt::format("Направление   : {}\n", this->direction);
	if(this->crossing_entry.is_set)
	{
		output << fmt::format("Вошел через   : {}\n", this->crossing_entry.status);
		output << fmt::format("Время         : {}\n", this->crossing_entry.timestamp);
	}
	if(this->crossing_exit.is_set)
	{
		output << fmt::format("Вышел через   : {}\n", this->crossing_exit.status);
		output << fmt::format("Время         : {}\n", this->crossing_exit.timestamp);
	}
	output << fmt::format("Скорость      : ~{} км/ч\n", (int)this->calculate_object_speed());
	for(const auto &lp : lp_data)
		output << fmt::format("Госномер      : {:>8} ({})\n", lp.label, lp.confidence);

	date = g_date_time_new_now_local();
	date_string = g_date_time_format(date, "%d%m%Y");
	std::string output_folder = fmt::format("{}/{}", output_path, date_string);
	g_free(date_string);
	g_date_time_unref(date);

	if(!std::filesystem::exists(output_folder))
		std::filesystem::create_directory(output_folder);

	std::string file_name = fmt::format("{}/analytics_{}.txt", output_folder, id);
	file.open(file_name);

	if(file.is_open())
	{
		file << output.str();
	}

	file.close();
}

[[nodiscard]] [[maybe_unused]]
double TrafficAnalysisData::calculate_object_speed(float lines_distance) const
{
	const double to_kmh{ 3.6 };

	if(!crossing_entry.is_set && !crossing_exit.is_set)
		return 0.0;

	double time_diff_ms = std::abs(crossing_exit.timestamp - crossing_entry.timestamp);

	if(time_diff_ms == 0)
		return 0.0;

	float distance_in_millimeters = lines_distance * 10e3;
	return (distance_in_millimeters / time_diff_ms) * to_kmh;
}

static bool
parse_user_metadata(AnalyticsConfig *, NvDsObjectMeta *obj_meta, GstBuffer *buffer, TrafficAnalysisData &data)
{
	bool success{};
	std::ofstream file;
	NvDsMetaList *l_user_meta;
	GstClockTime clock_time = buffer != nullptr ? buffer->pts : 0;

	// Access attached user meta for each object
	for(l_user_meta = obj_meta->obj_user_meta_list; l_user_meta != nullptr; l_user_meta = l_user_meta->next)
	{
		auto *user_meta = reinterpret_cast<NvDsUserMeta *>(l_user_meta->data);
		if(user_meta->base_meta.meta_type == NVDS_USER_OBJ_META_NVDSANALYTICS)
		{
			auto *user_meta_data = reinterpret_cast<NvDsAnalyticsObjInfo *>(user_meta->user_meta_data);
			bool has_lc_status{ !user_meta_data->lcStatus.empty() };

			if(has_lc_status)
			{
				success = true;
				const auto &lc_status = user_meta_data->lcStatus.at(0);
				if(!data.crossing_entry.is_set && data.crossing_entry.status != lc_status)
				{
					data.crossing_entry.status = lc_status;
					data.crossing_entry.timestamp = clock_time * 10e-6;
					data.crossing_entry.is_set = true;
				}
				else if(!data.crossing_exit.is_set && data.crossing_exit.status != lc_status)
				{
					data.crossing_exit.status = lc_status;
					data.crossing_exit.timestamp = clock_time * 10e-6;
					data.crossing_exit.is_set = true;
				}
			}

			if(data.passed_lines() && data.direction == TrafficAnalysisData::unknown_label)
			{
				if(!user_meta_data->dirStatus.empty())
				{

					auto dir_status = trim(user_meta_data->dirStatus);
					if(starts_with(dir_status, "DIR:"))
						dir_status = dir_status.substr(4);
					data.direction = dir_status;
					data.is_ready = true;
				}

				success = true;
			}
		}
	}

	return success;
}

static void parse_type_classifier_metadata(AnalyticsConfig *, NvDsObjectMeta *obj_meta, TrafficAnalysisData &data)
{
	NvDsMetaList *l_class{ obj_meta->classifier_meta_list };
	NvDsClassifierMeta *class_meta;
	for(; l_class; l_class = l_class->next)
	{
		class_meta = reinterpret_cast<NvDsClassifierMeta *>(l_class->data);
		if(!class_meta)
			continue;

		int label_i;
		NvDsLabelInfoList *l_label;
		for(label_i = 0, l_label = class_meta->label_info_list; label_i < class_meta->num_labels && l_label;
				label_i++, l_label = l_label->next)
		{
			auto label_info = reinterpret_cast<NvDsLabelInfo *>(l_label->data);
			if(label_info)
			{
				if(data.vehicle.confidence < label_info->result_prob)
				{
					data.vehicle.label = to_cyrillic(label_info->result_label);
					data.vehicle.confidence = label_info->result_prob;
				}
			}
		}
	}
}

/**
 * Parse metadata got by LPR gie.
 * */
static bool parse_lpr_classifier_metadata(AnalyticsConfig *config, NvDsObjectMeta *obj_meta, TrafficAnalysisData &data)
{
	int64_t id;

	bool success{};
	NvDsClassifierMetaList *l_class{ obj_meta->classifier_meta_list };

	id = obj_meta->parent != nullptr ? obj_meta->parent->object_id : -1;
	if(id >= 0)
	{
		for(; l_class; l_class = l_class->next)
		{
			auto class_meta = reinterpret_cast<NvDsClassifierMeta *>(l_class->data);
			if(!class_meta)
				continue;

			int label_i{};
			for(NvDsLabelInfoList *l_label = class_meta->label_info_list; label_i < class_meta->num_labels && l_label;
					label_i++, l_label = l_label->next)
			{
				if(auto label_info = reinterpret_cast<NvDsLabelInfo *>(l_label->data); label_info != nullptr)
				{
					if(label_info->label_id == 0 && label_info->result_class_id == 1)
					{
						int label_min_len = config->lp_min_length;
						size_t label_len = strlen(label_info->result_label);

						if(label_info->result_prob > 0.0)
						{
							if(label_len <= label_min_len)
								continue;

							auto license_plate_data = ClassifierData{ label_info->result_label, label_info->result_prob };
							data.lp_data.push_back(license_plate_data);
							success = true;
						}
					}
				}
			}
		}
	}

	return success;
}

static bool
parse_object_metadata(AnalyticsConfig *, NvDsObjectMeta *obj_meta, TrafficAnalysisData &) // NOLINT(*-no-recursion)
{
	NvBbox_Coords bbox;
	uint64_t id;
	std::ostringstream out_str;

	if(obj_meta == nullptr)
		return false;

	if(obj_meta->confidence == 0)
		return false;

	id = obj_meta->object_id;
	out_str << fmt::format("\tgie_id      : {}\n", obj_meta->unique_component_id);
	out_str << fmt::format("\tcls_id      : {}\n", obj_meta->class_id);
	out_str << fmt::format("\tlabel       : {}\n", obj_meta->obj_label);
	out_str << fmt::format("\tconf_d      : {}\n", obj_meta->confidence);
	out_str << fmt::format("\tconf_t      : {}\n", obj_meta->tracker_confidence);
	bbox = obj_meta->detector_bbox_info.org_bbox_coords;
	out_str << fmt::format("\tbbox_info_d : <l:{:>4}>;<t:{:>4}><w:{:>4}><h:{:>4}>\n", (int)bbox.left, (int)bbox.top,
												 (int)bbox.width, (int)bbox.height);
	bbox = obj_meta->tracker_bbox_info.org_bbox_coords;
	out_str << fmt::format("\tbbox_info_t : <l:{:>4}>;<t:{:>4}><w:{:>4}><h:{:>4}>\n", (int)bbox.left, (int)bbox.top,
												 (int)bbox.width, (int)bbox.height);
	if(obj_meta->text_params.display_text != nullptr)
		out_str << fmt::format("\ttext_params : {}\n", obj_meta->text_params.display_text);

	if(!out_str.str().empty() && obj_meta->confidence == -0.1)
	{
		TADS_DBG_MSG_V("%ld\n%s", id, out_str.str().c_str());
		out_str.clear();
	}

	return true;
}

bool create_analytics_bin(AnalyticsConfig *config, AnalyticsBin *analytics)
{
	bool success{};
	std::string elem_name{ "dsanalytics_bin" };

#ifdef TADS_ANALYTICS_DEBUG
	TADS_DBG_MSG_V("Creating bin element '%s'", elem_name.c_str());
#endif
	analytics->bin = gst::bin_new(elem_name);
	if(!analytics->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "dsanalytics_queue";

#ifdef TADS_ANALYTICS_DEBUG
	TADS_DBG_MSG_V("Creating queue element '%s'", elem_name.c_str());
#endif
	analytics->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!analytics->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "dsanalytics0";
#ifdef TADS_ANALYTICS_DEBUG
	TADS_DBG_MSG_V("Creating analytics_elem element '%s'", elem_name.c_str());
#endif
	analytics->analytics_elem = gst::element_factory_make(TADS_ELEM_DSANALYTICS_ELEMENT, elem_name);
	if(!analytics->analytics_elem)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add_many(GST_BIN(analytics->bin), analytics->queue, analytics->analytics_elem, nullptr);

	TADS_LINK_ELEMENT(analytics->queue, analytics->analytics_elem);

	TADS_BIN_ADD_GHOST_PAD(analytics->bin, analytics->queue, "sink");

	TADS_BIN_ADD_GHOST_PAD(analytics->bin, analytics->analytics_elem, "src");

	g_object_set(G_OBJECT(analytics->analytics_elem), "config-file", config->config_file_path.c_str(), nullptr);

	success = true;

	if(!analytics->timer)
		analytics->timer = g_timer_new();

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return success;
}

void parse_analytics_metadata(AppContext *app_context, GstBuffer *buffer, NvDsBatchMeta *batch_meta)
{
	NvDsMetaList *l_frame;
	NvDsMetaList *l_obj;
	int frame_num{}, obj_count{};
	auto &datamap = app_context->traffic_data_map;
	auto config = &app_context->config.analytics_config;
	auto output_path = app_context->config.analytics_config.output_path;

	for(l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next, frame_num++)
	{
		auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);
		if(!frame_meta)
			continue;

		for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next, obj_count++)
		{
			auto *obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			NvDsObjectMeta *parent_meta{};
			if(!obj_meta)
				continue;

			TrafficAnalysisData *data;
			uint64_t obj_id;
			std::string obj_label;

			if(obj_meta->parent != nullptr)
			{
				parent_meta = obj_meta->parent;
				obj_id = parent_meta->object_id;
			}
			else
			{
				obj_id = obj_meta->object_id;
			}

			if(datamap.count(obj_id) == 0)
			{
				datamap.try_emplace(obj_id, obj_id);
			}
			data = &datamap.at(obj_id);

			if(parent_meta != nullptr)
			{
				parse_lpr_classifier_metadata(config, obj_meta, *data);
				bool parsed = parse_user_metadata(config, parent_meta, buffer, *data);
				if(parsed)
					encode_image(&app_context->config.image_save_config, app_context->pipeline.common_elements.obj_enc_ctx_handle,
											 buffer);
				parse_object_metadata(config, parent_meta, *data);
			}
			else
			{
				parse_type_classifier_metadata(config, obj_meta, *data);
			}
		}
	}

	bool clear_datamap{ true };
	for(const auto &[id, data] : datamap)
	{
		if(data.ready())
		{
			save_image(&app_context->config.image_save_config, batch_meta);
			data.save_to_file(output_path);
			clear_datamap = true;
		}
		else
		{
			clear_datamap = false;
			continue;
		}
	}

	if(datamap.size() > 20 && clear_datamap)
	{
		datamap.clear();
	}
}