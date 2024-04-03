#ifndef TADS_SECONDARY_GIE_HPP
#define TADS_SECONDARY_GIE_HPP

#include "gie.hpp"

struct SecondaryGieBinSubBin : BaseBin
{
	GstElement *gie;
	GstElement *tee;
	GstElement *queue;
	GstElement *sink;

	bool create;
	uint num_children;
	int parent_index;
};

struct SecondaryGieBin : BaseBin
{
	GstElement *bin;
	GstElement *tee;
	GstElement *queue;
	uint64_t wait_for_sgie_process_buf_probe_id;
	bool stop;
	bool flush;
	std::vector<SecondaryGieBinSubBin> sub_bins{ MAX_SECONDARY_GIE_BINS };
	GMutex wait_lock;
	GCond wait_cond;
};

/**
 * Initialize @ref SecondaryGieBin.
 * It creates and adds secondary infer and other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under group @ref CONFIG_GROUP_SECONDARY_GIE
 *
 * @param[in] num_secondary_gie number of secondary infers.
 * @param[in] primary_gie_unique_id Unique id of primary infer to work on.
 * @param[in] configs array of pointers of type @ref NvDsGieConfig
 *            parsed from configuration file.
 * @param[in] bin pointer to @ref NvDsSecondaryGieBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_secondary_gie(uint num_secondary_gie, uint primary_gie_unique_id, const std::vector<GieConfig> &configs,
													SecondaryGieBin *bin);

/**
 * Release the resources.
 */
[[maybe_unused]]
void destroy_secondary_gie(SecondaryGieBin *bin);

#endif // TADS_SECONDARY_GIE_HPP
