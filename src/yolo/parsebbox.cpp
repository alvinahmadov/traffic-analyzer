#include <nvdsinfer_custom_impl.h>

#include "utils.hpp"

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
																	 NvDsInferNetworkInfo const &networkInfo,
																	 NvDsInferParseDetectionParams const &detectionParams,
																	 std::vector<NvDsInferParseObjectInfo> &objectList);

extern "C" bool NvDsInferParseYoloE(std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
																		NvDsInferNetworkInfo const &networkInfo,
																		NvDsInferParseDetectionParams const &detectionParams,
																		std::vector<NvDsInferParseObjectInfo> &objectList);

static NvDsInferParseObjectInfo
convertBBox(const float &bx1, const float &by1, const float &bx2, const float &by2, const uint &netW, const uint &netH)
{
	NvDsInferParseObjectInfo b;

	float x1 = bx1;
	float y1 = by1;
	float x2 = bx2;
	float y2 = by2;

	x1 = clamp(x1, 0, netW);
	y1 = clamp(y1, 0, netH);
	x2 = clamp(x2, 0, netW);
	y2 = clamp(y2, 0, netH);

	b.left = x1;
	b.width = clamp(x2 - x1, 0, netW);
	b.top = y1;
	b.height = clamp(y2 - y1, 0, netH);

	return b;
}

static void addBBoxProposal(const float bx1, const float by1, const float bx2, const float by2, const uint &net_width,
														const uint &net_height, const int max_index, const float max_prob,
														std::vector<NvDsInferParseObjectInfo> &binfo)
{
	NvDsInferParseObjectInfo bbi = convertBBox(bx1, by1, bx2, by2, net_width, net_height);

	if(bbi.width < 1 || bbi.height < 1)
	{
		return;
	}

	bbi.detectionConfidence = max_prob;
	bbi.classId = max_index;
	binfo.push_back(bbi);
}

static std::vector<NvDsInferParseObjectInfo>
decodeTensorYolo(const float *boxes, const float *scores, const float *classes, const uint &output_size,
								 const uint &net_width, const uint &net_height, const std::vector<float> &precluster_threshold)
{
	std::vector<NvDsInferParseObjectInfo> binfo;

	for(uint b = 0; b < output_size; ++b)
	{
		float max_prob = scores[b];
		int max_index = (int)classes[b];

		if(max_prob < precluster_threshold[max_index])
		{
			continue;
		}

		float bxc = boxes[b * 4 + 0];
		float byc = boxes[b * 4 + 1];
		float bw = boxes[b * 4 + 2];
		float bh = boxes[b * 4 + 3];

		float bx1 = bxc - bw / 2;
		float by1 = byc - bh / 2;
		float bx2 = bx1 + bw;
		float by2 = by1 + bh;

		addBBoxProposal(bx1, by1, bx2, by2, net_width, net_height, max_index, max_prob, binfo);
	}

	return binfo;
}

static std::vector<NvDsInferParseObjectInfo>
decodeTensorYoloE(const float *boxes, const float *scores, const float *classes, const uint &output_size,
									const uint &net_width, const uint &net_height, const std::vector<float> &precluster_threshold)
{
	std::vector<NvDsInferParseObjectInfo> binfo;

	for(uint b = 0; b < output_size; ++b)
	{
		float max_prob = scores[b];
		int max_index = (int)classes[b];

		if(max_prob < precluster_threshold[max_index])
		{
			continue;
		}

		float bx1 = boxes[b * 4 + 0];
		float by1 = boxes[b * 4 + 1];
		float bx2 = boxes[b * 4 + 2];
		float by2 = boxes[b * 4 + 3];

		addBBoxProposal(bx1, by1, bx2, by2, net_width, net_height, max_index, max_prob, binfo);
	}

	return binfo;
}

static bool NvDsInferParseCustomYolo(std::vector<NvDsInferLayerInfo> const &output_layers_info,
																		 NvDsInferNetworkInfo const &network_info,
																		 NvDsInferParseDetectionParams const &detection_params,
																		 std::vector<NvDsInferParseObjectInfo> &object_list)
{
	if(output_layers_info.empty())
	{
		TADS_ERR_MSG_V("Could not find output layer in bbox parsing");
		return false;
	}

	std::vector<NvDsInferParseObjectInfo> objects;

	const NvDsInferLayerInfo &boxes{ output_layers_info[0] };
	const NvDsInferLayerInfo &scores{ output_layers_info[1] };
	const NvDsInferLayerInfo &classes{ output_layers_info[2] };

	const uint output_size{ boxes.inferDims.d[0] };

	std::vector<NvDsInferParseObjectInfo> outObjs{ decodeTensorYolo(
			(const float *)(boxes.buffer), (const float *)(scores.buffer), (const float *)(classes.buffer), output_size,
			network_info.width, network_info.height, detection_params.perClassPreclusterThreshold) };

	objects.insert(objects.end(), outObjs.begin(), outObjs.end());

	object_list = objects;

	return true;
}

static bool NvDsInferParseCustomYoloE(std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
																			NvDsInferNetworkInfo const &networkInfo,
																			NvDsInferParseDetectionParams const &detection_params,
																			std::vector<NvDsInferParseObjectInfo> &object_list)
{
	if(outputLayersInfo.empty())
	{
		TADS_ERR_MSG_V("Could not find output layer in bbox parsing");
		return false;
	}

	std::vector<NvDsInferParseObjectInfo> objects;

	const NvDsInferLayerInfo &boxes{ outputLayersInfo[0] };
	const NvDsInferLayerInfo &scores{ outputLayersInfo[1] };
	const NvDsInferLayerInfo &classes{ outputLayersInfo[2] };

	const uint outputSize{ boxes.inferDims.d[0] };

	std::vector<NvDsInferParseObjectInfo> outObjs{ decodeTensorYoloE(
			(const float *)(boxes.buffer), (const float *)(scores.buffer), (const float *)(classes.buffer), outputSize,
			networkInfo.width, networkInfo.height, detection_params.perClassPreclusterThreshold) };

	objects.insert(objects.end(), outObjs.begin(), outObjs.end());

	object_list = objects;

	return true;
}

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
																	 NvDsInferNetworkInfo const &networkInfo,
																	 NvDsInferParseDetectionParams const &detectionParams,
																	 std::vector<NvDsInferParseObjectInfo> &objectList)
{
	return NvDsInferParseCustomYolo(outputLayersInfo, networkInfo, detectionParams, objectList);
}

extern "C" bool NvDsInferParseYoloE(std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
																		NvDsInferNetworkInfo const &networkInfo,
																		NvDsInferParseDetectionParams const &detectionParams,
																		std::vector<NvDsInferParseObjectInfo> &objectList)
{
	return NvDsInferParseCustomYoloE(outputLayersInfo, networkInfo, detectionParams, objectList);
}

CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYolo)	 // NOLINT(*-no-recursion)
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYoloE) // NOLINT(*-no-recursion)