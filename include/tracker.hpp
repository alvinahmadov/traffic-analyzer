#ifndef TADS_TRACKER_HPP
#define TADS_TRACKER_HPP

#include "common.hpp"

enum class TrackerComputeHW
{
	DEFAULT = 0,
	GPU = 1,
	VIC = 2
};

struct TrackerConfig
{
	/**
	 * Enable tracker
	 * */
	bool enable;
	/**
	 * Frame width at which the tracker is to operate, in pixels.
	 * (To be a multiple of 32 when visualTrackerType: 1 or
	 * reidType is non-zero with useVPICropScaler: 0)
	 *
	 * @example tracker-width=960
	 * */
	int tracker_width{ 640 };
	/**
	 * Frame height at which the tracker is to operate, in pixels.
	 * (To be a multiple of 32 when visualTrackerType: 1 or
	 * reidType is non-zero with useVPICropScaler: 0)
	 *
	 * @example tracker-height=544
	 * */
	int tracker_height{ 544 };
	/**
	 * Pathname of the low-level tracker library to be loaded
	 * by Gst-nvtracker.
	 * */
	std::string ll_lib_file;
	/**
	 * Configuration file for the low-level library if needed.
	 * */
	std::string ll_config_file;
	/**
	 * ID of the GPU on which device/unified memory is to be
	 * allocated, and with which buffer copy/scaling is to
	 * be done. (dGPU only.)
	 * */
	uint gpu_id;
	/**
	 * Set surface stream type for tracking.
	 * */
	uint tracking_surface_type{};
	/**
	 * Enables tracking ID display on OSD.
	 * */
	bool display_tracking_id{ true };
	/**
	 * Compute engine to use for scaling.
	 * 0 - Default
	 * 1 - GPU
	 * 2 - VIC (Jetson only)
	 * */
	TrackerComputeHW compute_hw{ TrackerComputeHW::DEFAULT };
	/**
	 * Allow force-reset of tracking ID based on pipeline event.
	 * Once tracking ID reset is enabled and such event happens,
	 * the lower 32-bit of the tracking ID will be reset to 0:
	 *
	 * - 0: Not reset tracking ID when stream reset or EOS event happens
	 * - 1: Terminate all existing trackers and assign new IDs for a
	 * stream when the stream reset happens (i.e., GST_NVEVENT_STREAM_RESET)
	 * - 2: Let tracking ID start from 0 after receiving EOS event
	 * (i.e., GST_NVEVENT_STREAM_EOS)
	 * (Note: Only the lower 32-bit of tracking ID to start from 0)
	 * - 3: Enable both option 1 and 2
	 * */
	uint tracking_id_reset_mode{};
	/**
	 * Use the tensor-meta from Gst-nvdspreprocess if available
	 * for tensor-meta-gie-id
	 * */
	bool input_tensor_meta{};
	/**
	 * Tensor Meta GIE ID to be used, property valid only if
	 * input-tensor-meta is TRUE
	 * */
	uint input_tensor_gie_id{};
	uint user_meta_pool_size{ 16 };
	/**
	 * Configures splitting of a batch of frames in sub-batches
	 *
	 * Semicolon delimited integer array.
	 * Must include all values from 0 to (batch-size -1) where
	 * batch-size is configured in [streammux].
	 * */
	std::string sub_batches;
};

struct TrackerBin
{
	GstElement *bin;
	GstElement *tracker;
};

/**
 * Initialize @ref NvDsTrackerBin. It creates and adds tracker and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_TRACKER
 *
 * @param[in] config pointer of type @ref NvDsTrackerConfig
 *            parsed from configuration file.
 * @param[in] bin pointer of type @ref NvDsTrackerBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_tracking_bin(TrackerConfig *config, TrackerBin *bin);

#endif // TADS_TRACKER_HPP
