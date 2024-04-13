#ifndef TADS_IMAGE_SAVE_HPP
#define TADS_IMAGE_SAVE_HPP

#include <nvds_obj_encode.h>

#include "common.hpp"

struct AppContext;

struct ImageSaveConfig
{
	bool enable{};
	uint gpu_id{};
	std::string output_folder_path{};
	bool save_image_cropped_object{ true };
	std::string frame_to_skip_rules_path{};
	uint second_to_skip_interval{ 600 };
	uint quality{ 80 };
	double min_confidence{};
	double max_confidence{ 1.0 };
	uint min_box_width{};
	uint min_box_height{};
};

/**
 * save_detected_object_image will extract metadata received on OSD sink pad
 * and update params for drawing rectangle, object information. We also iterate
 * through the user meta of type "NVDS_CROP_IMAGE_META" to find image crop meta
 * and demonstrate how to access it.
 * */
bool save_image(ImageSaveConfig *config, NvDsObjectMeta *obj_meta, const std::string &filename);

/**
 * encode_image will extract metadata received on pgie src pad
 * and update params for drawing rectangle, object information etc. We also
 * iterate through the object list and encode the cropped objects as jpeg
 * images and attach it as user meta to the respective objects.
 * */
GstPadProbeReturn encode_image(AppContext* app_context, GstBuffer *buffer);

#endif // TADS_IMAGE_SAVE_HPP
