#include <cmath>

#include <fmt/format.h>

#include "utils.hpp"
#include "layers/batchnorm.hpp"

bool batchnormLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
										std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
										nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string layer_name, activation;
	nvinfer1::IScaleLayer *batchnorm;
	int filters, size;
	std::vector<float> bn_biases, bn_weights, bn_running_mean, bn_running_var;
	float *shift_wt, *scale_wt, *power_wt;
	nvinfer1::Weights shift, scale, power;

	if(block.at("type") != "batchnorm")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'batchnorm'", block.at("type").c_str());
		goto done;
	}

	if(block.find("filters") == block.end())
	{
		TADS_ERR_MSG_V("Filters not found in block type '%s'", block.at("type").c_str());
		goto done;
	}

	filters = std::stoi(block.at("filters"));
	activation = block.at("activation");

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

	size = filters;
	shift = { nvinfer1::DataType::kFLOAT, nullptr, size };
	scale = { nvinfer1::DataType::kFLOAT, nullptr, size };
	power = { nvinfer1::DataType::kFLOAT, nullptr, size };
	shift_wt = new float[size];
	for(int i = 0; i < size; ++i)
		shift_wt[i] = bn_biases.at(i) - ((bn_running_mean.at(i) * bn_weights.at(i)) / bn_running_var.at(i));
	shift.values = shift_wt;
	scale_wt = new float[size];
	for(int i = 0; i < size; ++i)
		scale_wt[i] = bn_weights.at(i) / bn_running_var[i];
	scale.values = scale_wt;
	power_wt = new float[size];
	for(int i = 0; i < size; ++i)
		power_wt[i] = 1.0;
	power.values = power_wt;
	trt_weights.push_back(shift);
	trt_weights.push_back(scale);
	trt_weights.push_back(power);

	batchnorm = network->addScale(*input, nvinfer1::ScaleMode::kCHANNEL, shift, scale, power);
	layer_name = fmt::format("batchnorm_{}", index);

	if(!batchnorm)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	batchnorm->setName(layer_name.c_str());
	output = batchnorm->getOutput(0);

	success = activationLayer(output, index, activation, output, network);
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