#ifndef TADS_SECONDARY_PREPROCESS_HPP
#define TADS_SECONDARY_PREPROCESS_HPP

#include "preprocess.hpp"

struct SecondaryPreProcessSubBin : BaseBin
{
	GstElement *queue;
	GstElement *secondary_preprocess;
	GstElement *tee;
	GstElement *sink;

	bool create;
	uint num_children;
	int parent_index;
};

struct SecondaryPreProcessBin : BaseBin
{
	GstElement *bin;
	GstElement *tee;
	GstElement *queue;

	uint64_t wait_for_secondary_preprocess_process_buf_probe_id;
	bool stop;
	bool flush;
	std::vector<SecondaryPreProcessSubBin> sub_bins{ MAX_SECONDARY_GIE_BINS };
	GMutex wait_lock;
	GCond wait_cond;
};

/**
 * Initialize @ref SecondaryPreProcessBin. It creates and adds secondary preprocess and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_SECONDARY_PREPROCESS
 *
 * @param[in] num_secondary_gie number of secondary preprocess.
 * @param[in] configs array of pointers of type @ref NvDsPreProcessConfig
 *            parsed from configuration file.
 * @param[in] bin pointer to @ref NvDsSecondaryPreProcessBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_secondary_preprocess_bin(uint num_secondary_preprocess, uint primary_gie_unique_id,
																		 const std::vector<PreProcessConfig> &configs, SecondaryPreProcessBin *bin);

/**
 * Release the resources.
 */
[[maybe_unused]]
void destroy_secondary_preprocess_bin(SecondaryPreProcessBin *bin);

#endif // TADS_SECONDARY_PREPROCESS_HPP
