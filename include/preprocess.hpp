#ifndef TADS_PREPROCESS_HPP
#define TADS_PREPROCESS_HPP

#include "common.hpp"

struct PreProcessConfig : BaseConfig
{
	bool enable;																		///< enable preprocessing
	[[maybe_unused]] int operate_on_gie_id;				///< gie id on which preprocessing is to be done
	[[maybe_unused]] bool is_operate_on_gie_id_set; ///<
	std::string config_file_path;										///< config file path having properties for preprocess
};

struct PreProcessBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *preprocess;
};

/**
 * Initialize @ref NvDsPreProcessBin. It creates and adds preprocess and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_PREPROCESS
 *
 * @param[in] config pointer to infer @ref NvDsPreProcessConfig parsed from
 *            configuration file.
 * @param[in] preprocess_bin pointer to @ref NvDsPreProcessBin to be filled.
 *
 * @return true if bin created successfully.
 */
bool create_preprocess_bin(PreProcessConfig *config, PreProcessBin *preprocess_bin);

#endif // TADS_PREPROCESS_HPP
