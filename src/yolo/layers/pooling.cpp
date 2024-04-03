#include <fmt/format.h>

#include "utils.hpp"
#include "layers/pooling.hpp"

bool poolingLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network)
{
	bool success{};
	std::string_view block_type{ block.at("type") };
	std::string layer_name;

	nvinfer1::IPoolingLayer *maxpool;
	nvinfer1::IPoolingLayer *avgpool;

	int size;
	int stride;

	if(block_type != "max" || block_type != "maxpool" || block_type != "avg" || block_type != "avgpool")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match required 'deconvolutional'", block_type.data());
		goto done;
	}

	if(block_type == "max" || block_type == "maxpool")
	{
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

		size = std::stoi(block.at("size"));
		stride = std::stoi(block.at("stride"));

		maxpool = network->addPoolingNd(*input, nvinfer1::PoolingType::kMAX,
																		nvinfer1::Dims{
																				2, { size, size }
		});
		layer_name = fmt::format("maxpool_{}", index);

		if(!maxpool)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		maxpool->setName(layer_name.c_str());
		maxpool->setStrideNd(nvinfer1::Dims{
				2, { stride, stride }
		});
		maxpool->setPaddingNd(nvinfer1::Dims{
				2, { (size - 1) / 2, (size - 1) / 2 }
		});
		if(size == 2 && stride == 1)
		{
			maxpool->setPrePadding(nvinfer1::Dims{
					2, { 0, 0 }
			});
			maxpool->setPostPadding(nvinfer1::Dims{
					2, { 1, 1 }
			});
		}
		output = maxpool->getOutput(0);
	}
	else if(block_type == "avg" || block_type == "avgpool")
	{
		nvinfer1::Dims input_dims{ input->getDimensions() };
		avgpool = network->addPoolingNd(*input, nvinfer1::PoolingType::kAVERAGE,
																		nvinfer1::Dims{
																				2, { input_dims.d[1], input_dims.d[2] }
		});
		layer_name = fmt::format("avgpool_{}", index);

		if(!avgpool)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		avgpool->setName(layer_name.c_str());
		output = avgpool->getOutput(0);
	}
	else
	{
		TADS_ERR_MSG_V("Pooling not supported: %s", block_type.data());
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