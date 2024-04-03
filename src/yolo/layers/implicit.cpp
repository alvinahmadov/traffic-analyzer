#include <fmt/format.h>

#include "utils.hpp"
#include "layers/implicit.hpp"

bool implicitLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
									 nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string_view block_type{ block.at("type") };
	std::string layer_name;

	nvinfer1::IConstantLayer *implicit;

	float *val;
	nvinfer1::Weights conv_wt;

	int filters;

	if(block_type != "implicit" || block_type != "implicit_add" || block_type != "implicit_mul")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match any of 'implicit' | 'implicit_add' | 'implicit_mul'", block_type.data());
		goto done;
	}
	if(block.find("filters") == block.end())
	{
		TADS_ERR_MSG_V("Filters not found");
		goto done;
	}

	filters = std::stoi(block.at("filters"));

	conv_wt = { nvinfer1::DataType::kFLOAT, nullptr, filters };

	val = new float[filters];
	for(int i = 0; i < filters; ++i)
	{
		val[i] = weights[weight_ptr];
		++weight_ptr;
	}
	conv_wt.values = val;
	trt_weights.push_back(conv_wt);

	implicit = network->addConstant(
			nvinfer1::Dims{
					4, { 1, filters, 1, 1 }
	},
			conv_wt);
	layer_name = fmt::format("{}_{}", block_type, index);

	if(!implicit)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	implicit->setName(layer_name.c_str());
	output = implicit->getOutput(0);

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}