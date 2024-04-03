#include <fmt/format.h>

#include "utils.hpp"
#include "layers/shortcut.hpp"

bool shortcutLayer(nvinfer1::ITensor *&output, int index, std::string_view activation, std::string_view input_vol,
									 std::string_view shortcut_vol, const std::map<std::string, std::string> &block,
									 nvinfer1::ITensor *input, nvinfer1::ITensor *shortcut_input, nvinfer1::INetworkDefinition *network,
									 uint batch_size)
{
	bool success{};
	std::string_view block_type{ block.at("type") };

	std::string layer_name;
	nvinfer1::IElementWiseLayer *shortcut;

	if(block_type != "shortcut")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'shortcut'", block_type.data());
		goto done;
	}

	if(input_vol != shortcut_vol)
	{
		std::string name{ "slice" };
		nvinfer1::Dims start{
			4, { 0, 0, 0, 0 }
		};
		nvinfer1::Dims size{ input->getDimensions() };
		nvinfer1::Dims stride{
			4, { 1, 1, 1, 1 }
		};

		if(!sliceLayer(output, index, name, shortcut_input, start, size, stride, network, batch_size))
		{
			goto done;
		}
	}
	else
	{
		output = shortcut_input;
	}

	shortcut = network->addElementWise(*input, *output, nvinfer1::ElementWiseOperation::kSUM);
	layer_name = fmt::format("shortcut_{}", index);

	if(!shortcut)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	shortcut->setName(layer_name.c_str());
	output = shortcut->getOutput(0);

	if(!activationLayer(output, index, activation, output, network))
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