#include "preprocess.hpp"

bool create_preprocess_bin(PreProcessConfig *config, PreProcessBin *preprocess_bin)
{
	bool success{};
	std::string elem_name{ "preprocess" };

	preprocess_bin->bin = gst::bin_new(elem_name);
	if(!preprocess_bin->bin)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "preprocess_queue";
	preprocess_bin->queue = gst::element_factory_make(TADS_ELEM_QUEUE, elem_name);
	if(!preprocess_bin->queue)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	elem_name = "preprocess0";
	preprocess_bin->preprocess = gst::element_factory_make(TADS_ELEM_PREPROCESS, elem_name);
	if(!preprocess_bin->preprocess)
	{
		TADS_ERR_MSG_V("Failed to create '%s'", elem_name.c_str());
		goto done;
	}

	gst_bin_add_many(GST_BIN(preprocess_bin->bin), preprocess_bin->queue, preprocess_bin->preprocess, nullptr);

	TADS_LINK_ELEMENT(preprocess_bin->queue, preprocess_bin->preprocess);

	TADS_BIN_ADD_GHOST_PAD(preprocess_bin->bin, preprocess_bin->queue, "sink");

	TADS_BIN_ADD_GHOST_PAD(preprocess_bin->bin, preprocess_bin->preprocess, "src");

	g_object_set(G_OBJECT(preprocess_bin->preprocess), "config-file", config->config_file_path.c_str(), nullptr);

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return success;
}
