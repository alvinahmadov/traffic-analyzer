#include <fmt/format.h>

#include "utils.hpp"
#include "layers/channels.hpp"

bool channelsLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 nvinfer1::ITensor *input, nvinfer1::ITensor *implicit_tensor, nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string layer_name;
	std::string_view block_type{ block.at("type") };

	if(block_type != "shift_channels" || block_type != "control_channels")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'shift_channels' or 'control_channels'", block_type.data());
		goto done;
	}

	if(block_type == "shift_channels")
	{
		nvinfer1::IElementWiseLayer *shift =
				network->addElementWise(*input, *implicit_tensor, nvinfer1::ElementWiseOperation::kSUM);
		layer_name = fmt::format("shift_channels_{}", index);

		if(!shift)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		shift->setName(layer_name.c_str());
		output = shift->getOutput(0);
	}
	else if(block_type == "control_channels")
	{
		nvinfer1::IElementWiseLayer *control =
				network->addElementWise(*input, *implicit_tensor, nvinfer1::ElementWiseOperation::kPROD);
		layer_name = fmt::format("control_channels_{}", index);

		if(!control)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		control->setName(layer_name.c_str());
		output = control->getOutput(0);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}