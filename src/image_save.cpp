#include <cstring>
#include <fstream>
#include <glib.h>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>

#include "image_save.hpp"
#include "app.hpp"

bool save_image(ImageSaveConfig *config, NvDsObjectMeta *obj_meta, const std::string &filename)
{
	NvDsUserMetaList *user_meta_list;
	std::ofstream file;

	bool success{};

	if(config->output_folder_path.empty())
		return false;

	/* To verify  encoded metadata of cropped objects, we iterate through the
	 * user metadata of each object and if a metadata of the type
	 * 'NVDS_CROP_IMAGE_META' is found then we write that to a file as
	 * implemented below.
	 */
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

				if(!fs::exists(filename))
				{
					file.open(filename, std::ios::binary);
					if(file.is_open())
					{
#ifdef TADS_ANALYTICS_DEBUG
						TADS_DBG_MSG_V("Saving cropped image to '%s'", filename.c_str());
#endif
						file.write(reinterpret_cast<char *>(enc_jpeg_image->outBuffer), enc_jpeg_image->outLen);
						success = true;
					}
					file.close();
					break;
				}
			}
		}
	}

	return success;
}

GstPadProbeReturn encode_image(AppContext *app_context, GstBuffer *buffer)
{
	const ImageSaveConfig *config = &app_context->config.image_save_config;
	NvDsObjEncCtxHandle ctx_handle = app_context->pipeline.common_elements.obj_enc_ctx_handle;

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

		int obj_num{};
		for(l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next, obj_num++)
		{
			obj_meta = reinterpret_cast<NvDsObjectMeta *>(l_obj->data);
			NvBbox_Coords bbox_coords = obj_meta->detector_bbox_info.org_bbox_coords;

			if(config->save_image_cropped_object)
			{
				bool matches_conf_reqs{ (config->min_confidence <= obj_meta->confidence) &&
																(obj_meta->confidence <= config->max_confidence) };
				bool matches_coord_reqs{ (bbox_coords.width >= config->min_box_width) &&
																 (bbox_coords.height >= config->min_box_height) };

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