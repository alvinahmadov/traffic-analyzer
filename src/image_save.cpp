#include <cstring>
#include <fstream>
#include <filesystem>
#include <glib.h>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>

#include "image_save.hpp"

namespace fs = std::filesystem;

[[maybe_unused]]
void save_image(ImageSaveConfig *config, NvDsBatchMeta *batch_meta)
{
	NvDsObjectMeta *obj_meta;
	NvDsMetaList *l_frame;
	NvDsMetaList *l_obj;
	std::ofstream file;
	std::string output_folder_path;
	char *date_string;
	GDateTime *date;

	if(!config->enable)
		return;

	if(config->output_folder_path.empty())
		return;
	date = g_date_time_new_now_local();
	date_string = g_date_time_format(date, "%d%m%Y");
	output_folder_path = fmt::format("{}/{}", config->output_folder_path, date_string);
	g_free(date_string);
	g_date_time_unref(date);

	if(!std::filesystem::exists(output_folder_path))
		std::filesystem::create_directory(output_folder_path);

	for(l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next)
	{
		auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);
		/* To verify  encoded metadata of cropped frames, we iterate through the
		 * user metadata of each frame and if a metadata of the type
		 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
		 * implemented below.
		 */

		if(config->save_image_full_frame)
		{
			std::string frame_filename;
			save_image(config, frame_meta, frame_filename);
		}

		for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next)
		{
			obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			/* To verify  encoded metadata of cropped objects, we iterate through the
			 * user metadata of each object and if a metadata of the type
			 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
			 * implemented below.
			 */
			if(config->save_image_cropped_object)
			{
				std::string filename;
				save_image(config, obj_meta, filename);
			}
		}
	}
}

void save_image(ImageSaveConfig *config, NvDsFrameMeta *frame_meta, std::string &output_folder_path)
{
	NvDsObjectMeta *obj_meta;
	NvDsMetaList *l_obj;
	NvDsUserMetaList *user_meta_list;
	std::ofstream file;

	if(!config->enable)
		return;

	if(config->output_folder_path.empty())
		return;

	if(!std::filesystem::exists(output_folder_path))
		std::filesystem::create_directory(output_folder_path);

	/* To verify  encoded metadata of cropped frames, we iterate through the
	 * user metadata of each frame and if a metadata of the type
	 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
	 * implemented below.
	 */
	std::string frame_filename;

	if(config->save_image_full_frame)
	{
		for(user_meta_list = frame_meta->frame_user_meta_list; user_meta_list != nullptr;
				user_meta_list = user_meta_list->next)
		{
			auto *user_meta = reinterpret_cast<NvDsUserMeta *>(user_meta_list->data);
			if(user_meta->base_meta.meta_type == NVDS_CROP_IMAGE_META)
			{
				frame_filename =
						fmt::format("{}/frame_{}_{}.jpg", output_folder_path, frame_meta->frame_num, frame_meta->batch_id);
				auto *enc_jpeg_image = reinterpret_cast<NvDsObjEncOutParams *>(user_meta->user_meta_data);
				/* Write to File */
				file.open(frame_filename, std::ios::binary);
				if(file.is_open())
				{
					file.write(reinterpret_cast<char *>(enc_jpeg_image->outBuffer), enc_jpeg_image->outLen);
				}
				file.close();
			}
		}
	}

	for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next)
	{
		obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
		/* To verify  encoded metadata of cropped objects, we iterate through the
		 * user metadata of each object and if a metadata of the type
		 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
		 * implemented below.
		 */
		std::string obj_filename;

		/* write metadata to jpeg images of vehicles. */
		if(config->save_image_cropped_object)
		{
			user_meta_list = obj_meta->obj_user_meta_list;
			for(; user_meta_list != nullptr; user_meta_list = user_meta_list->next)
			{
				auto *user_meta = reinterpret_cast<NvDsUserMeta *>(user_meta_list->data);
				if(user_meta && user_meta->base_meta.meta_type == NVDS_CROP_IMAGE_META)
				{
					auto *enc_jpeg_image = reinterpret_cast<NvDsObjEncOutParams *>(user_meta->user_meta_data);

					if(!enc_jpeg_image)
						continue;

					obj_filename = fmt::format("{}/{}_{}_{}.jpg", output_folder_path, obj_meta->obj_label, obj_meta->object_id,
																		 frame_meta->batch_id);

					file.open(obj_filename, std::ios::binary);
					if(file.is_open())
					{
						file.write(reinterpret_cast<char *>(enc_jpeg_image->outBuffer), enc_jpeg_image->outLen);
					}
					file.close();
				}
			}
		}
	}
}

void save_image(ImageSaveConfig *config, NvDsObjectMeta *obj_meta, std::string &filename)
{
	NvDsUserMetaList *user_meta_list;
	std::ofstream file;
	GDateTime *date_time;
	std::string output_folder_path;

	if(!config->enable)
		return;

	if(config->output_folder_path.empty())
		return;

	/* To verify  encoded metadata of cropped objects, we iterate through the
	 * user metadata of each object and if a metadata of the type
	 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
	 * implemented below.
	 */
	/* write metadata to jpeg images of vehicles. */
	if(config->save_image_cropped_object)
	{
		date_time = g_date_time_new_now_local();
		char *date_string = g_date_time_format(date_time, "%d%m%Y");
		output_folder_path = fmt::format("{}/{}", config->output_folder_path, date_string);

		if(!fs::exists(output_folder_path))
			fs::create_directory(output_folder_path);

		user_meta_list = obj_meta->obj_user_meta_list;
		for(; user_meta_list != nullptr; user_meta_list = user_meta_list->next)
		{
			auto *user_meta = reinterpret_cast<NvDsUserMeta *>(user_meta_list->data);
			if(user_meta && user_meta->base_meta.meta_type == NVDS_CROP_IMAGE_META)
			{
				auto *enc_jpeg_image = reinterpret_cast<NvDsObjEncOutParams *>(user_meta->user_meta_data);

				if(!enc_jpeg_image)
					continue;

				filename.clear();
				filename = fmt::format("{}/obj_{}_{}.jpg", output_folder_path, obj_meta->object_id, obj_meta->obj_label);
				file.open(filename, std::ios::binary);
				if(file.is_open())
				{
					file.write(reinterpret_cast<char *>(enc_jpeg_image->outBuffer), enc_jpeg_image->outLen);
				}
				file.close();
			}
		}

		g_free(date_string);
		g_date_time_unref(date_time);
	}
}

GstPadProbeReturn encode_image(ImageSaveConfig *config, NvDsObjEncCtxHandle ctx_handle, GstBuffer *buffer)
{
	if(!config->enable)
		return GST_PAD_PROBE_OK;

	GstMapInfo inmap = GST_MAP_INFO_INIT;
	NvDsObjectMeta *obj_meta;
	NvDsMetaList *l_frame;
	NvDsMetaList *l_obj;
	NvDsBatchMeta *batch_meta;

	if(!gst_buffer_map(buffer, &inmap, GST_MAP_READ))
	{
		GST_ERROR("input buffer mapinfo failed");
		return GST_PAD_PROBE_DROP;
	}
	auto *ip_surf = reinterpret_cast<NvBufSurface *>(inmap.data);
	gst_buffer_unmap(buffer, &inmap);

	batch_meta = gst_buffer_get_nvds_batch_meta(buffer);

	for(l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next)
	{
		auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(l_frame->data);

		if(config->save_image_full_frame)
		{
			NvDsObjEncUsrArgs frameData{};
			frameData.isFrame = true;
			frameData.saveImg = false;
			frameData.attachUsrMeta = true;
			frameData.quality = config->quality;
			nvds_obj_enc_process(ctx_handle, &frameData, ip_surf, nullptr, frame_meta);
		}

		int obj_num{};
		for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next, obj_num++)
		{
			obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			NvBbox_Coords bbox_coords = obj_meta->detector_bbox_info.org_bbox_coords;

			if(config->save_image_cropped_object)
			{
				bool matches_conf_reqs{ config->min_confidence <= obj_meta->confidence &&
																obj_meta->confidence <= config->max_confidence };
				bool matches_coord_reqs{ bbox_coords.width >= config->min_box_width &&
																 bbox_coords.height >= config->min_box_height };

				if(matches_coord_reqs && matches_conf_reqs)
				{
					NvDsObjEncUsrArgs obj_meta_data{};
					// To be set by user
					obj_meta_data.objNum = obj_num;
					obj_meta_data.saveImg = false;
					obj_meta_data.attachUsrMeta = true;
					obj_meta_data.quality = config->quality;
					snprintf(obj_meta_data.fileNameImg, FILE_NAME_SIZE, "%s/obj_%ld.jpg", config->output_folder_path.c_str(),
									 obj_meta->object_id);
					nvds_obj_enc_process(ctx_handle, &obj_meta_data, ip_surf, obj_meta, frame_meta);
				}
			}
		}
	}

	nvds_obj_enc_finish(ctx_handle);
	return GST_PAD_PROBE_OK;
}