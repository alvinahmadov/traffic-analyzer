#include <fmt/format.h>

#include "utils.hpp"
#include "layers/upsample.hpp"

bool upsampleLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string_view block_type{ block.at("type") };
	const size_t scale_size{ 4 };

	int stride;
	float *scale;
	std::string layer_name;
	nvinfer1::IResizeLayer *resize;

	if(block_type != "upsample")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'upsample'", block_type.data());
		goto done;
	}

	if(block.find("stride") == block.end())
	{
		TADS_ERR_MSG_V("Stride not found");
		goto done;
	}

	stride = std::stoi(block.at("stride"));
	scale = new float[scale_size]{ 1, 1, static_cast<float>(stride), static_cast<float>(stride) };

	resize = network->addResize(*input);
	layer_name = fmt::format("upsample_{}", index);

	if(!resize)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	resize->setName(layer_name.c_str());
	resize->setResizeMode(nvinfer1::ResizeMode::kNEAREST);
	resize->setScales(scale, 4);
	output = resize->getOutput(0);

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}