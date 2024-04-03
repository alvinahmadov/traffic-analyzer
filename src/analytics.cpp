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
	lc_top{},
	lc_bottom{},
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
	fmt::print("Класс         : {}\n", this->classifier_data.label);
	for(const auto &lp : lp_data)
	{
		fmt::print("Госномер      : {} ({})\n", lp.label, lp.confidence);
	}
	fmt::print("Направление   : {}\n", this->direction);
	if(this->lc_top.is_set)
	{
		fmt::print("Точка:{}    : {}\n", this->lc_top.status, this->lc_top.timestamp);
	}
	if(this->lc_bottom.is_set)
	{
		fmt::print("Точка:{} : {}\n", this->lc_bottom.status, this->lc_bottom.timestamp);
	}
	fmt::print("Скорость      : ~{} км/ч\n", (int)this->calculate_object_speed());
}

void TrafficAnalysisData::save_to_file(const std::string &output_path) const
{
	std::ostringstream output;
	std::ofstream file;

	if(output_path.empty())
		return;

	std::string filename;
	auto paths = split(this->img_filename, "../");
	if(paths.size() >= 2)
	{
		filename = join(paths, "/");
	}
	else
	{
		filename = this->img_filename;
	}

	output << fmt::format("Номер    : {}\n", this->id);
	output << fmt::format("Класс    : {}\n", this->classifier_data.label);

	output << fmt::format("Напр.    : {}\n", this->direction);
	if(this->lc_top.is_set)
	{
	output << fmt::format("Точка1   : {}\n", this->lc_top.time_str);
	}
	if(this->lc_bottom.is_set)
	{
	output << fmt::format("Точка2   : {}\n", this->lc_bottom.time_str);
	}
	output << fmt::format("Скорость : ~{} км/ч\n", (int)this->calculate_object_speed());
	output << fmt::format("Файл     : {}\n", filename);
	for(const auto &lp : lp_data)
		output << fmt::format("Госномер : {:>8} ({})\n", lp.label, lp.confidence);

	if(!std::filesystem::exists(output_path))
		std::filesystem::create_directory(output_path);

	std::string file_name = fmt::format("{}/analytics_{}.txt", output_path, id);
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

	if(!lc_top.is_set && !lc_bottom.is_set)
		return 0.0;

	double time_diff_ms = std::abs(lc_bottom.timestamp - lc_top.timestamp);

	if(time_diff_ms == 0)
		return 0.0;

	float distance_in_millimeters = lines_distance * 10e3;
	return (distance_in_millimeters / time_diff_ms) * to_kmh;
}

static bool
parse_user_metadata(AppContext *app_context, NvDsObjectMeta *obj_meta, GstBuffer *buffer, TrafficAnalysisData &data)
{
	bool success{};
	std::ofstream file;
	NvDsMetaList *l_user_meta;
	GstClockTime clock_time = buffer != nullptr ? buffer->pts : 0;
	std::string_view date_time_format{ "%H:%M:%S.%f %d-%m-%Y" };
	GDateTime *date_time = g_date_time_new_now_local();
	ImageSaveConfig *config{ &app_context->config.image_save_config };
	char *date_time_string = g_date_time_format(date_time, date_time_format.data());

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
				for(const auto& lc_status : user_meta_data->lcStatus)
				{
					if(!data.lc_top.is_set && data.lc_top.status != lc_status)
					{
						data.lc_top.status = lc_status;
						data.lc_top.timestamp = clock_time * 10e-6;
						data.lc_top.time_str = date_time_string;
						data.lc_top.is_set = true;
					}
					else if(!data.lc_bottom.is_set && data.lc_bottom.status != lc_status)
					{
						data.lc_bottom.status = lc_status;
						data.lc_bottom.timestamp = clock_time * 10e-6;
						data.lc_bottom.time_str = date_time_string;
						data.lc_bottom.is_set = true;
					}
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
				encode_image(config, app_context->pipeline.common_elements.obj_enc_ctx_handle, buffer);
			}
		}
	}

	if(data.ready())
	{
		auto output_path = config->output_folder_path;
		save_image(config, obj_meta, data.img_filename);
	}

	g_free(date_time_string);
	g_date_time_unref(date_time);
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
				if(data.classifier_data.confidence < label_info->result_prob)
				{
					data.classifier_data.label = to_cyrillic(label_info->result_label);
					data.classifier_data.confidence = label_info->result_prob;
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

						if(label_info->result_prob > 0.0 && data.lp_data.size() < 10)
						{
							if(label_len <= label_min_len)
								continue;
							data.lp_data.emplace_back(label_info->result_label, label_info->result_prob);
							success = true;
						}
					}
				}
			}
		}
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
				parse_user_metadata(app_context, parent_meta, buffer, *data);
				parse_lpr_classifier_metadata(config, obj_meta, *data);
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
			GDateTime *date = g_date_time_new_now_local();
			char *date_string = g_date_time_format(date, "%d%m%Y");
			std::string output_folder = fmt::format("{}/{}", output_path, date_string);
			g_free(date_string);
			g_date_time_unref(date);

			if(!std::filesystem::exists(output_folder))
				std::filesystem::create_directory(output_folder);

			data.save_to_file(output_folder);
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