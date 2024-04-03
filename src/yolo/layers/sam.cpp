#include <fmt/format.h>

#include "utils.hpp"
#include "layers/sam.hpp"

bool samLayer(nvinfer1::ITensor *&output, int index, std::string_view activation,
							const std::map<std::string, std::string> &block, nvinfer1::ITensor *input, nvinfer1::ITensor *sam_input,
							nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string_view block_type{ block.at("type") };

	std::string layer_name;
	nvinfer1::IElementWiseLayer *sam_layer;

	if(block_type != "sam")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'sam'", block_type.data());
		goto done;
	}

	sam_layer = network->addElementWise(*input, *sam_input, nvinfer1::ElementWiseOperation::kPROD);
	layer_name = fmt::format("sam_{}", index);

	if(!sam_layer)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	sam_layer->setName(layer_name.c_str());
	output = sam_layer->getOutput(0);

	activationLayer(output, index, activation, output, network);

	if(!output)
	{
		goto done;
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}