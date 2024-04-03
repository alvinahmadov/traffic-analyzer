#ifndef TADS_IMAGE_SAVE_HPP
#define TADS_IMAGE_SAVE_HPP

#include "common.hpp"

#if NVDS_VERSION_MINOR >= 4
#include <nvds_obj_encode.h>
#endif

struct ImageSaveConfig
{
	bool enable{};
	uint gpu_id{};
	std::string output_folder_path{};
	bool save_image_full_frame{ false };
	bool save_image_cropped_object{ true };
	std::string frame_to_skip_rules_path{};
	uint second_to_skip_interval{ 600 };
	uint quality{ 80 };
	double min_confidence{ 0.0 };
	double max_confidence{ 1.0 };
	uint min_box_width{ 1 };
	uint min_box_height{ 1 };
};

/**
 * save_detected_object_image will extract metadata received on OSD sink pad
 * and update params for drawing rectangle, object information. We also iterate
 * through the user meta of type "NVDS_CROP_IMAGE_META" to find image crop meta
 * and demonstrate how to access it.
 * */
void save_image(ImageSaveConfig *, NvDsBatchMeta *batch_meta);

/**
 * encode_image will extract metadata received on pgie src pad
 * and update params for drawing rectangle, object information etc. We also
 * iterate through the object list and encode the cropped objects as jpeg
 * images and attach it as user meta to the respective objects.
 * */
GstPadProbeReturn encode_image(ImageSaveConfig *config, NvDsObjEncCtxHandle ctx_handle, GstBuffer* buffer);

#endif // TADS_IMAGE_SAVE_HPP
