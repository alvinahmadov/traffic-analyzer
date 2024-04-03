#ifndef TADS_PRIMARY_GIE_HPP
#define TADS_PRIMARY_GIE_HPP

#include "gie.hpp"

/**
 * Primary GPU Inference Engine
 * */
struct PrimaryGieBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *nvvidconv;
	GstElement *gie;
};

/**
 * Initialize @ref NvDsPrimaryGieBin. It creates and adds primary infer and
 * other elements needed for processing to the primary_gie.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_PRIMARY_GIE
 *
 * @param[in] config pointer to infer @ref NvDsGieConfig parsed from
 *            configuration file.
 * @param[in] bin pointer to @ref NvDsPrimaryGieBin to be filled.
 *
 * @return true if primary_gie created successfully.
 */
bool create_primary_gie(GieConfig *config, PrimaryGieBin *bin);

#endif // TADS_PRIMARY_GIE_HPP
