#include <nvdsinfer_custom_impl.h>

[[maybe_unused]]
bool NvDsInferInitializeInputLayers(std::vector<NvDsInferLayerInfo> const &inputLayersInfo,
																		NvDsInferNetworkInfo const &networkInfo, unsigned int maxBatchSize)
{
	auto *scale_factor = (float *)inputLayersInfo[0].buffer;
	for(unsigned int i = 0; i < maxBatchSize; i++)
	{
		scale_factor[i * 2 + 0] = 1.0;
		scale_factor[i * 2 + 1] = 1.0;
	}
	return true;
}