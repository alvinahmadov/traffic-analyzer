#include <vector>

#include <fmt/format.h>

#include "utils.hpp"
#include "layers/reorg.hpp"

bool reorgLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
								nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network, uint batch_size)
{
	bool success{};
	int stride{ 1 };
	std::string_view block_type{ block.at("type") };

	std::string layer_name;
	nvinfer1::Dims input_dims;

	if(block_type != "reorg" || block_type != "reorg3d")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match any of 'reorg' | 'reorg3d'", block_type.data());
		goto done;
	}

	if(block.find("stride") != block.end())
	{
		stride = std::stoi(block.at("stride"));
	}

	input_dims = input->getDimensions();

	if(block_type == "reorg3d")
	{
		std::string name1{ "slice1" }, name2{ "slice2" }, name3{ "slice3" }, name4{ "slice4" };

		nvinfer1::ITensor *slice1{}, *slice2{}, *slice3{}, *slice4{};

		nvinfer1::Dims start1{
			4, { 0, 0, 0, 0 }
		};
		nvinfer1::Dims start2{
			4, { 0, 0, 0, 1 }
		};
		nvinfer1::Dims start3{
			4, { 0, 0, 1, 0 }
		};
		nvinfer1::Dims start4{
			4, { 0, 0, 1, 1 }
		};
		nvinfer1::Dims size_all{
			4, { input_dims.d[0], input_dims.d[1], input_dims.d[2] / stride, input_dims.d[3] / stride }
		};
		nvinfer1::Dims stride_all{
			4, { 1, 1, stride, stride }
		};

		if(!sliceLayer(slice1, index, name1, input, start1, size_all, stride_all, network, batch_size))
		{
			goto done;
		}

		if(!sliceLayer(slice2, index, name2, input, start2, size_all, stride_all, network, batch_size))
		{
			goto done;
		}

		if(!sliceLayer(slice3, index, name3, input, start3, size_all, stride_all, network, batch_size))
		{
			goto done;
		}

		if(!sliceLayer(slice4, index, name4, input, start4, size_all, stride_all, network, batch_size))
		{
			goto done;
		}

		std::vector<nvinfer1::ITensor *> concat_inputs;
		concat_inputs.push_back(slice1);
		concat_inputs.push_back(slice2);
		concat_inputs.push_back(slice3);
		concat_inputs.push_back(slice4);

		nvinfer1::IConcatenationLayer *concat{ network->addConcatenation(concat_inputs.data(), concat_inputs.size()) };
		layer_name = fmt::format("concat_{}", index);

		if(!concat)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}
		concat->setName(layer_name.c_str());
		concat->setAxis(0);
		output = concat->getOutput(0);
	}
	else
	{
		nvinfer1::IShuffleLayer *shuffle1 = network->addShuffle(*input);
		layer_name = fmt::format("shuffle1_{}", index);

		if(!shuffle1)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}
		shuffle1->setName(layer_name.c_str());
		nvinfer1::Dims reshapeDims1{
			6, { input_dims.d[0], input_dims.d[1] / (stride * stride), input_dims.d[2], stride, input_dims.d[3], stride }
		};
		shuffle1->setReshapeDimensions(reshapeDims1);
		nvinfer1::Permutation permutation1{
			{ 0, 1, 2, 4, 3, 5 }
		};
		shuffle1->setSecondTranspose(permutation1);
		output = shuffle1->getOutput(0);

		nvinfer1::IShuffleLayer *shuffle2 = network->addShuffle(*output);
		layer_name = fmt::format("shuffle2_{}", index);

		if(!shuffle2)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		shuffle2->setName(layer_name.c_str());
		nvinfer1::Dims reshapeDims2{
			4, { input_dims.d[0], input_dims.d[1] / (stride * stride), input_dims.d[2] * input_dims.d[3], stride * stride }
		};
		shuffle2->setReshapeDimensions(reshapeDims2);
		nvinfer1::Permutation permutation2{
			{ 0, 1, 3, 2 }
		};
		shuffle2->setSecondTranspose(permutation2);
		output = shuffle2->getOutput(0);

		// shuffle3
		nvinfer1::IShuffleLayer *shuffle3 = network->addShuffle(*output);
		layer_name = fmt::format("shuffle3_{}", index);
		if(!shuffle3)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		shuffle3->setName(layer_name.c_str());
		nvinfer1::Dims reshapeDims3{
			4, { input_dims.d[0], input_dims.d[1] / (stride * stride), stride * stride, input_dims.d[2] * input_dims.d[3] }
		};
		shuffle3->setReshapeDimensions(reshapeDims3);
		nvinfer1::Permutation permutation3{
			{ 0, 2, 1, 3 }
		};
		shuffle3->setSecondTranspose(permutation3);
		output = shuffle3->getOutput(0);

		nvinfer1::IShuffleLayer *shuffle4 = network->addShuffle(*output);
		layer_name = fmt::format("shuffle4_{}", index);
		if(!shuffle4)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		shuffle4->setName(layer_name.c_str());
		nvinfer1::Dims reshapeDims4{
			4, { input_dims.d[0], input_dims.d[1] * stride * stride, input_dims.d[2] / stride, input_dims.d[3] / stride }
		};
		shuffle4->setReshapeDimensions(reshapeDims4);
		output = shuffle4->getOutput(0);
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}