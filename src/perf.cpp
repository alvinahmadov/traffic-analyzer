#include <cmath>
#include <sys/time.h>

#include <gstnvdsmeta.h>

#include "perf.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantFunctionResult"
/**
 * Buffer probe function on sink element.
 */
static GstPadProbeReturn sink_bin_buf_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, void *data)
{
	auto *str = reinterpret_cast<AppPerfStructInt *>(data);
	NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(GST_BUFFER(info->data));

	if(!batch_meta)
		return GST_PAD_PROBE_OK;

	if(!str->stop)
	{
		g_mutex_lock(&str->struct_lock);
		for(NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next)
		{
			auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);
			InstancePerfStruct *str1 = &str->instance_str[frame_meta->pad_index];
			gettimeofday(&str1->last_fps_time, nullptr);
			if(str1->start_fps_time.tv_sec == 0 && str1->start_fps_time.tv_usec == 0)
			{
				str1->start_fps_time = str1->last_fps_time;
			}
			else
			{
				str1->buffer_cnt++;
			}
		}
		g_mutex_unlock(&str->struct_lock);
	}
	return GST_PAD_PROBE_OK;
}
#pragma clang diagnostic pop

static bool perf_measurement_callback(void *data)
{
	auto *str = reinterpret_cast<AppPerfStructInt *>(data);
	std::array<uint, MAX_SOURCE_BINS> buffer_cnt{ 0 };
	AppPerfStruct perf_struct;
	struct timeval current_fps_time;
	uint i;
	g_mutex_lock(&str->struct_lock);
	if(str->stop)
	{
		g_mutex_unlock(&str->struct_lock);
		return false;
	}
	perf_struct.use_nvmultiurisrcbin = str->use_nvmultiurisrcbin;
	perf_struct.stream_name_display = str->stream_name_display;
	/*if (str->use_nvmultiurisrcbin) {
		perf_struct.num_instances =  g_hash_table_size(str->FPSInfoHash);
	} else {
		perf_struct.num_instances = str->num_instances;
	}*/

	if(!str->use_nvmultiurisrcbin)
	{
		for(i = 0; i < str->num_instances; i++)
		{
			buffer_cnt.at(i) = str->instance_str[i].buffer_cnt / str->dewarper_surfaces_per_frame;
			str->instance_str[i].buffer_cnt = 0;
		}
	}
	else
	{
		GList *active_source_id_list = g_hash_table_get_keys(str->fps_info_hash);
		GList *temp = active_source_id_list;
		for(uint j = 0; j < g_hash_table_size(str->fps_info_hash); j++)
		{
			(perf_struct.source_detail[j]).source_id = GPOINTER_TO_UINT(temp->data);
			auto *fps_sensor_info = static_cast<FPSSensorInfo *>(
					g_hash_table_lookup(str->fps_info_hash, GUINT_TO_POINTER((perf_struct.source_detail[j]).source_id)));
			(perf_struct.source_detail[j]).stream_name = (char *)fps_sensor_info->uri;
			temp = temp->next;
		}

		if(temp)
			g_list_free(temp);

		if(active_source_id_list)
			g_list_free(active_source_id_list);
		perf_struct.active_source_size = g_hash_table_size(str->fps_info_hash);

		for(uint j = 0; j < g_hash_table_size(str->fps_info_hash); j++)
		{
			i = perf_struct.source_detail[j].source_id;
			buffer_cnt.at(i) = str->instance_str[i].buffer_cnt / str->dewarper_surfaces_per_frame;
			str->instance_str[i].buffer_cnt = 0;
		}
	}

	perf_struct.num_instances = str->num_instances;
	gettimeofday(&current_fps_time, nullptr);

	if(!str->use_nvmultiurisrcbin)
	{
		for(i = 0; i < str->num_instances; i++)
		{
			InstancePerfStruct *str1 = &str->instance_str[i];
			double time1 = (str1->total_fps_time.tv_sec + str1->total_fps_time.tv_usec / 1000000.0) +
										 (current_fps_time.tv_sec + current_fps_time.tv_usec / 1000000.0) -
										 (str1->start_fps_time.tv_sec + str1->start_fps_time.tv_usec / 1000000.0);

			double time2;

			if(str1->last_sample_fps_time.tv_sec == 0 && str1->last_sample_fps_time.tv_usec == 0)
			{
				time2 = (str1->last_fps_time.tv_sec + str1->last_fps_time.tv_usec / 1000000.0) -
								(str1->start_fps_time.tv_sec + str1->start_fps_time.tv_usec / 1000000.0);
			}
			else
			{
				time2 = (str1->last_fps_time.tv_sec + str1->last_fps_time.tv_usec / 1000000.0) -
								(str1->last_sample_fps_time.tv_sec + str1->last_sample_fps_time.tv_usec / 1000000.0);
			}
			str1->total_buffer_cnt += buffer_cnt.at(i);
			perf_struct.fps.at(i) = buffer_cnt.at(i) / time2;
			if(std::isnan(perf_struct.fps.at(i)))
				perf_struct.fps[i] = 0;

			perf_struct.fps_avg.at(i) = str1->total_buffer_cnt / time1;
			if(std::isnan(perf_struct.fps_avg.at(i)))
				perf_struct.fps_avg.at(i) = 0;

			str1->last_sample_fps_time = str1->last_fps_time;
		}
	}
	else
	{
		for(uint j = 0; j < g_hash_table_size(str->fps_info_hash); j++)
		{
			i = perf_struct.source_detail[j].source_id;
			InstancePerfStruct *str1 = &str->instance_str[i];
			double time1 = (str1->total_fps_time.tv_sec + str1->total_fps_time.tv_usec / 1000000.0) +
										 (current_fps_time.tv_sec + current_fps_time.tv_usec / 1000000.0) -
										 (str1->start_fps_time.tv_sec + str1->start_fps_time.tv_usec / 1000000.0);

			double time2;

			if(str1->last_sample_fps_time.tv_sec == 0 && str1->last_sample_fps_time.tv_usec == 0)
			{
				time2 = (str1->last_fps_time.tv_sec + str1->last_fps_time.tv_usec / 1000000.0) -
								(str1->start_fps_time.tv_sec + str1->start_fps_time.tv_usec / 1000000.0);
			}
			else
			{
				time2 = (str1->last_fps_time.tv_sec + str1->last_fps_time.tv_usec / 1000000.0) -
								(str1->last_sample_fps_time.tv_sec + str1->last_sample_fps_time.tv_usec / 1000000.0);
			}
			str1->total_buffer_cnt += buffer_cnt.at(i);
			perf_struct.fps.at(i) = buffer_cnt.at(i) / time2;
			if(std::isnan(perf_struct.fps.at(i)))
				perf_struct.fps.at(i) = 0;

			perf_struct.fps_avg.at(i) = str1->total_buffer_cnt / time1;
			if(std::isnan(perf_struct.fps_avg.at(i)))
				perf_struct.fps_avg.at(i) = 0;

			str1->last_sample_fps_time = str1->last_fps_time;
		}
	}
	g_mutex_unlock(&str->struct_lock);

	if(str->callback != nullptr)
		str->callback(str->context, &perf_struct);

	return true;
}

void pause_perf_measurement(AppPerfStructInt *perf_struct)
{
	uint i;

	g_mutex_lock(&perf_struct->struct_lock);
	perf_struct->stop = true;

	for(i = 0; i < perf_struct->num_instances; i++)
	{
		InstancePerfStruct *str1 = &perf_struct->instance_str[i];
		str1->total_fps_time.tv_sec += str1->last_fps_time.tv_sec - str1->start_fps_time.tv_sec;
		str1->total_fps_time.tv_usec += str1->last_fps_time.tv_usec - str1->start_fps_time.tv_usec;
		if(str1->total_fps_time.tv_usec < 0)
		{
			str1->total_fps_time.tv_sec--;
			str1->total_fps_time.tv_usec += 1000000;
		}
		str1->start_fps_time.tv_sec = str1->start_fps_time.tv_usec = 0;
	}

	g_mutex_unlock(&perf_struct->struct_lock);
}

void resume_perf_measurement(AppPerfStructInt *perf_struct)
{
	g_mutex_lock(&perf_struct->struct_lock);
	if(!perf_struct->stop)
	{
		g_mutex_unlock(&perf_struct->struct_lock);
		return;
	}

	perf_struct->stop = false;

	for(uint i{}; i < perf_struct->num_instances; i++)
	{
		perf_struct->instance_str[i].buffer_cnt = 0;
	}

	if(!perf_struct->perf_measurement_timeout_id)
		perf_struct->perf_measurement_timeout_id = g_timeout_add(
				perf_struct->measurement_interval_ms, reinterpret_cast<GSourceFunc>(perf_measurement_callback), perf_struct);

	g_mutex_unlock(&perf_struct->struct_lock);
}

bool enable_perf_measurement(AppPerfStructInt *str, GstPad *sink_bin_pad, uint num_sources, gulong interval_sec,
														 /*uint num_surfaces_per_frame,*/ perf_callback callback)
{
	if(!callback)
	{
		return false;
	}

	str->num_instances = num_sources;

	str->measurement_interval_ms = interval_sec * 1000;
	str->dewarper_surfaces_per_frame = 1;
	str->callback = callback;
	str->stop = true;

//	if(num_surfaces_per_frame)
//	{
//		str->dewarper_surfaces_per_frame = num_surfaces_per_frame;
//	}

	for(uint i{}; i < num_sources; i++)
	{
		str->instance_str[i].buffer_cnt = 0;
	}
	str->sink_bin_pad = sink_bin_pad;
	str->fps_measure_probe_id =
			gst_pad_add_probe(sink_bin_pad, GST_PAD_PROBE_TYPE_BUFFER, sink_bin_buf_probe, str, nullptr);

	resume_perf_measurement(str);

	return true;
}
