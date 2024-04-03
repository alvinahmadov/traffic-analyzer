#include <cmath>
#include <fmt/format.h>

#include "utils.hpp"
#include "layers/convolutional.hpp"

bool convolutionalLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
												std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
												int &input_channels, nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network,
												const std::string &name)
{
	bool success{};
	bool batch_normalize{};
	std::string_view block_type{ block.at("type") };
	nvinfer1::IConvolutionLayer *conv;

	std::string layer_name;
	std::string activation;
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

	if(block_type != "convolutional" || block_type != "c2f")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match any of 'convolutional' | 'c2f'", block_type.data());
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
	activation = block.at("activation");
	bias = filters;

	if(block.find("batch_normalize") != block.end())
	{
		bias = 0;
		batch_normalize = (block.at("batch_normalize") == "1");
	}

	if(block.find("bias") != block.end())
	{
		bias = std::stoi(block.at("bias"));
		if(bias == 1)
			bias = filters;
	}

	if(block.find("groups") != block.end())
		groups = std::stoi(block.at("groups"));

	int pad;
	if(padding)
		pad = (kernelSize - 1) / 2;
	else
		pad = 0;

	size = filters * input_channels * kernelSize * kernelSize / groups;
	conv_wt = { nvinfer1::DataType::kFLOAT, nullptr, size };
	conv_bias = { nvinfer1::DataType::kFLOAT, nullptr, bias };

	if(!batch_normalize)
	{
		float *val;
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
	}
	else
	{
		for(int i = 0; i < filters; ++i)
		{
			bn_biases.push_back(weights[weight_ptr]);
			++weight_ptr;
		}
		for(int i = 0; i < filters; ++i)
		{
			bn_weights.push_back(weights[weight_ptr]);
			++weight_ptr;
		}
		for(int i = 0; i < filters; ++i)
		{
			bn_running_mean.push_back(weights[weight_ptr]);
			++weight_ptr;
		}
		for(int i = 0; i < filters; ++i)
		{
			bn_running_var.push_back(sqrt(weights[weight_ptr] + 1.0e-5));
			++weight_ptr;
		}
		float *val;
		if(bias != 0)
		{
			val = new float[filters];
			for(int i = 0; i < filters; ++i)
			{
				val[i] = weights[weight_ptr];
				++weight_ptr;
			}
			conv_bias.values = val;
		}
		val = new float[size];
		for(int i = 0; i < size; ++i)
		{
			val[i] = weights[weight_ptr];
			++weight_ptr;
		}
		conv_wt.values = val;
		trt_weights.push_back(conv_wt);
		if(bias != 0)
			trt_weights.push_back(conv_bias);
	}

	conv = network->addConvolutionNd(*input, filters,
																	 nvinfer1::Dims{
																			 2, { kernelSize, kernelSize }
	 },
																	 conv_wt, conv_bias);
	layer_name = fmt::format("conv_{}{}", name, index);

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

	if(batch_normalize)
	{
		size = filters;
		nvinfer1::Weights shift{ nvinfer1::DataType::kFLOAT, nullptr, size };
		nvinfer1::Weights scale{ nvinfer1::DataType::kFLOAT, nullptr, size };
		nvinfer1::Weights power{ nvinfer1::DataType::kFLOAT, nullptr, size };
		auto *shift_wt = new float[size];
		for(int i = 0; i < size; ++i)
			shift_wt[i] = bn_biases.at(i) - ((bn_running_mean.at(i) * bn_weights.at(i)) / bn_running_var.at(i));
		shift.values = shift_wt;
		auto *scale_wt = new float[size];
		for(int i = 0; i < size; ++i)
			scale_wt[i] = bn_weights.at(i) / bn_running_var[i];
		scale.values = scale_wt;
		auto *power_wt = new float[size];
		for(int i = 0; i < size; ++i)
			power_wt[i] = 1.0;
		power.values = power_wt;
		trt_weights.push_back(shift);
		trt_weights.push_back(scale);
		trt_weights.push_back(power);

		nvinfer1::IScaleLayer *batchnorm = network->addScale(*output, nvinfer1::ScaleMode::kCHANNEL, shift, scale, power);
		layer_name = fmt::format("batchnorm_{}{}", name, index);

		if(!batchnorm)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}
		batchnorm->setName(layer_name.c_str());
		output = batchnorm->getOutput(0);
	}

	success = activationLayer(output, index, activation, output, network, name);
	if(!success)
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