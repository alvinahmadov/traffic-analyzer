#include <fmt/format.h>

#include "utils.hpp"
#include "layers/deconvolutional.hpp"

bool deconvolutionalLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
													std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
													int &input_channels, nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network,
													const std::string &name)
{
	bool success{};
	std::string_view block_type{ block.at("type") };

	nvinfer1::IDeconvolutionLayer *conv;
	std::string layer_name;
	std::vector<float> bn_biases;
	std::vector<float> bn_weights;
	std::vector<float> bn_running_mean;
	std::vector<float> bn_running_var;

	nvinfer1::Weights conv_wt;
	nvinfer1::Weights conv_bias;

	int groups{ 1 };
	int filters;
	int padding;
	int kernelSize;
	int stride;
	int bias;
	int size;
	int pad{};
	float *val;

	if(block_type != "deconvolutional")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match required 'deconvolutional'", block_type.data());
		goto done;
	}
	if(block.find("filters") == block.end())
	{
		TADS_ERR_MSG_V("Filters not found");
		goto done;
	}

	if(block.find("pad") == block.end())
	{
		TADS_ERR_MSG_V("Pad not found");
		goto done;
	}

	if(block.find("size") == block.end())
	{
		TADS_ERR_MSG_V("Size not found");
		goto done;
	}

	if(block.find("stride") == block.end())
	{
		TADS_ERR_MSG_V("Stride not found");
		goto done;
	}

	filters = std::stoi(block.at("filters"));
	padding = std::stoi(block.at("pad"));
	kernelSize = std::stoi(block.at("size"));
	stride = std::stoi(block.at("stride"));
	bias = filters;

	if(block.find("groups") != block.end())
		groups = std::stoi(block.at("groups"));

	if(block.find("bias") != block.end())
		bias = std::stoi(block.at("bias"));

	if(padding)
		pad = (kernelSize - 1) / 2;

	size = filters * input_channels * kernelSize * kernelSize / groups;
	conv_wt = { nvinfer1::DataType::kFLOAT, nullptr, size };
	conv_bias = { nvinfer1::DataType::kFLOAT, nullptr, bias };

	if(bias != 0)
	{
		val = new float[filters];
		for(int i = 0; i < filters; ++i)
		{
			val[i] = weights[weight_ptr];
			++weight_ptr;
		}
		conv_bias.values = val;
		trt_weights.push_back(conv_bias);
	}
	val = new float[size];
	for(int i = 0; i < size; ++i)
	{
		val[i] = weights[weight_ptr];
		++weight_ptr;
	}
	conv_wt.values = val;
	trt_weights.push_back(conv_wt);

	conv = network->addDeconvolutionNd(*input, filters,
																		 nvinfer1::Dims{
																				 2, { kernelSize, kernelSize }
	 },
																		 conv_wt, conv_bias);
	layer_name = fmt::format("deconv_{}{}", name, index);

	if(!conv)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	conv->setName(layer_name.c_str());
	conv->setStrideNd(nvinfer1::Dims{
			2, { stride, stride }
	});
	conv->setPaddingNd(nvinfer1::Dims{
			2, { pad, pad }
	});

	if(block.find("groups") != block.end())
		conv->setNbGroups(groups);

	output = conv->getOutput(0);

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}