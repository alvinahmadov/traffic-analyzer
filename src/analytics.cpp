#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nvds_analytics_meta.h>

#include "analytics.hpp"
#include "image_save.hpp"
#include "app.hpp"

static uint64_t g_data_index{};

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
	has_image{},
	direction{ TrafficAnalysisData::unknown_label },
	output_path{ "./" }
{
	index = g_data_index++;
}

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

	if(auto speed = this->get_object_speed(); speed > 0)
	{
		fmt::print("Скорость      : ~{} км/ч\n", speed);
	}
	else
	{
		fmt::print("Скорость      : N/A\n");
	}
}

void TrafficAnalysisData::save_to_file() const
{
	std::ostringstream output;
	std::ofstream file;

	if(output_path.empty())
		return;
	std::string filename = fmt::format("{}/analytics_{}.txt", output_path, id);

	if(fs::exists(filename))
		return;

	output << fmt::format("Номер    : {}\n", this->id);
	output << fmt::format("Класс    : {}\n", this->classifier_data.label);
	output << fmt::format("Напр.    : {}\n", this->direction);

	if(this->lc_top.is_set)
	{
		output << fmt::format("Точка1   : {:<6} - {}\n", this->lc_top.status, this->lc_top.time_str);
	}
	else
	{
		output << std::string("Точка1   : N/A\n");
	}
	if(this->lc_bottom.is_set)
	{
		output << fmt::format("Точка2   : {:<6} - {}\n", this->lc_bottom.status, this->lc_bottom.time_str);
	}
	else
	{
		output << std::string("Точка2   : N/A\n");
	}

	if(auto speed = this->get_object_speed(); speed > 0)
	{
		output << fmt::format("Скорость : ~{} км/ч\n", speed);
	}
	else
	{
		output << std::string("Скорость : N/A\n");
	}

	if(this->has_image)
	{
		std::string image_file_name{ this->get_image_filename() };
		auto chunks = split(image_file_name, "../");
		image_file_name = join(chunks, "/");
		output << fmt::format("Изобр.   : {}\n", image_file_name);
	}
	else
	{
		output << std::string("Изобр.   : N/A\n");
	}

	if(!lp_data.empty())
	{
		auto iter = std::max_element(lp_data.cbegin(), lp_data.cend(),
																 [](const auto &prev, const auto &curr) { return curr.confidence > prev.confidence; });
		output << fmt::format("Госномер : {:>8} ({})\n", iter->label, iter->confidence);
	}
	else
	{
		output << std::string("Госномер : N/A (0)\n");
	}

	file.open(filename);

	if(file.is_open())
	{
#ifdef TADS_ANALYTICS_DEBUG
		TADS_DBG_MSG_V("Saving analytics log to '%s'", filename.c_str());
#endif
		file << output.str();
		output.clear();
	}

	file.close();
}

[[nodiscard]] [[maybe_unused]]
int TrafficAnalysisData::get_object_speed() const
{
	const double to_kmh{ 3.6 };

	if(!lc_top.is_set && !lc_bottom.is_set)
		return 0.0;

	double time_diff_ms = std::abs(lc_bottom.timestamp - lc_top.timestamp);

	if((int)time_diff_ms == 0)
		return 0.0;

	float distance_in_millimeters = distance * 10e3;
	return (distance_in_millimeters / time_diff_ms) * to_kmh;
}

std::string TrafficAnalysisData::get_image_filename() const
{
	return fmt::format("{}/obj_{}.jpg", this->output_path, this->id);
}

static bool
parse_user_metadata(AppContext *app_context, NvDsObjectMeta *obj_meta, GstBuffer *buffer, TrafficAnalysisData &data)
{
	bool success{};
	std::ofstream file;
	NvDsMetaList *l_user_meta;
	ImageSaveConfig *image_save_config = &app_context->config.image_save_config;
	GTimer *timer = app_context->pipeline.common_elements.analytics.timer;

	auto get_timestamp = [&timer, &buffer]()
	{
		if(timer != nullptr)
			return g_timer_elapsed(timer, nullptr) * 10e3;
		return buffer->pts * 10e-6;
	};

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
				for(const auto &lc_status : user_meta_data->lcStatus)
				{
					if(!data.lc_top.is_set && data.lc_top.status != lc_status)
					{
						data.lc_top.status = lc_status;
						data.lc_top.timestamp = get_timestamp();
						data.lc_top.time_str = get_current_date_time_str();
						data.lc_top.is_set = true;
#ifdef TADS_ANALYTICS_DEBUG
						TADS_DBG_MSG_V("Object %lu crossed line %s at %s", data.id, data.lc_top.status.c_str(),
													 data.lc_top.time_str.c_str());
#endif
					}
					else if(!data.lc_bottom.is_set && data.lc_bottom.status != lc_status)
					{
						data.lc_bottom.status = lc_status;
						data.lc_bottom.timestamp = get_timestamp();
						data.lc_bottom.time_str = get_current_date_time_str();
						data.lc_bottom.is_set = true;
#ifdef TADS_ANALYTICS_DEBUG
						TADS_DBG_MSG_V("Object %lu crossed line %s at %s", data.id, data.lc_bottom.status.c_str(),
													 data.lc_bottom.time_str.c_str());
#endif
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
#ifdef TADS_ANALYTICS_DEBUG
					TADS_DBG_MSG_V("Object %lu direction: '%s'", data.id, data.direction.c_str());
#endif
				}

				success = true;
				encode_image(app_context, buffer);
			}
		}
	}

	if(data.ready() && image_save_config->enable)
	{
		data.has_image = save_image(image_save_config, obj_meta, data.get_image_filename());
	}

	return success;
}

static void parse_type_classifier_metadata(AppContext *, NvDsObjectMeta *obj_meta, TrafficAnalysisData &data)
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
#ifdef TADS_ANALYTICS_DEBUG
					TADS_DBG_MSG_V("Object %lu class: '%s'", data.id, data.classifier_data.label.c_str());
#endif
				}
			}
		}
	}
}

/**
 * Parse metadata got by LPR gie.
 * */
static bool parse_lpr_classifier_metadata(AppContext *app_context, NvDsObjectMeta *obj_meta, TrafficAnalysisData &data)
{
	bool success{};
	NvDsClassifierMetaList *l_class{ obj_meta->classifier_meta_list };
	AnalyticsConfig *config = &app_context->config.analytics_config;

	for(; l_class; l_class = l_class->next)
	{
		auto class_meta = reinterpret_cast<NvDsClassifierMeta *>(l_class->data);
		if(!class_meta)
			continue;

		int label_i{};
		for(NvDsLabelInfoList *l_label = class_meta->label_info_list; label_i < class_meta->num_labels && l_label;
				label_i++, l_label = l_label->next)
		{
			if(data.id != obj_meta->parent->object_id)
				continue;

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
#ifdef TADS_ANALYTICS_DEBUG
						TADS_DBG_MSG_V("Object %lu LP: '%s'", data.id, data.lp_data.back().label.c_str());
#endif
						success = true;
					}
				}
			}
		}
	}

	return success;
}

void parse_object_metadata(AppContext *app_context, GstBuffer *buffer, NvDsObjectMeta *obj_meta,
													 const std::string &output_path)
{
	uint64_t obj_id;
	std::string obj_label;
	TrafficAnalysisData *data;
	NvDsObjectMeta *parent_meta;
	auto &traffic_data_map = app_context->pipeline.common_elements.analytics.traffic_data_map;

	if(obj_meta->parent != nullptr)
	{
		parent_meta = obj_meta->parent;
		obj_id = parent_meta->object_id;
	}
	else
	{
		parent_meta = nullptr;
		obj_id = obj_meta->object_id;
	}

	if(auto iter = traffic_data_map.find(obj_id); iter == traffic_data_map.end())
	{
		if(traffic_data_map.size() > MAX_ANALYTICS_DATA_LENGTH)
		{
			auto pos_iter = traffic_data_map.begin();
			std::advance(pos_iter, MAX_ANALYTICS_DATA_LENGTH - MIN_ANALYTICS_DATA_LENGTH);
			traffic_data_map.erase(traffic_data_map.begin(), pos_iter);
		}
		auto result = traffic_data_map.try_emplace(obj_id, obj_id);

		if(result.second)
			data = &result.first->second;
		else
			data = nullptr;
	}
	else
	{
		data = &iter->second;
	}

	if(data != nullptr)
	{
		data->output_path = output_path;
		if(parent_meta != nullptr)
		{
			parse_user_metadata(app_context, parent_meta, buffer, *data);
			parse_lpr_classifier_metadata(app_context, obj_meta, *data);
		}
		else
		{
			parse_type_classifier_metadata(app_context, obj_meta, *data);
		}
	}
}

void parse_analytics_metadata(AppContext *app_context, GstBuffer *buffer, NvDsBatchMeta *batch_meta)
{
	NvDsMetaList *l_frame;
	NvDsMetaList *l_obj;
	int frame_num{}, obj_count{};
	auto &datamap = app_context->pipeline.common_elements.analytics.traffic_data_map;
	auto output_path = app_context->config.analytics_config.output_path;
	GDateTime *date_time = app_context->pipeline.common_elements.analytics.date_time;
	if(date_time)
	{
		char *date_string = g_date_time_format(date_time, "%d%m%Y");
		output_path = fmt::format("{}/{}", output_path, date_string);
		g_free(date_string);
	}
	else
	{
		TADS_WARN_MSG_V("Couldn't get GDateTime element");
	}

	if(!std::filesystem::exists(output_path))
	{
		std::filesystem::create_directory(output_path);
	}

	if(TrafficAnalysisData::distance < 0)
	{
		TrafficAnalysisData::distance = app_context->config.analytics_config.lines_distance;
	}

	for(l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next, frame_num++)
	{
		auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);
		if(!frame_meta)
			continue;

		for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next, obj_count++)
		{
			auto *obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			parse_object_metadata(app_context, buffer, obj_meta, output_path);
		}
	}

	for(const auto &[id, data] : datamap)
	{
		if(data.ready())
		{
			data.save_to_file();
		}
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
	if(!analytics->date_time)
		analytics->date_time = g_date_time_new_now_local();

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return success;
}