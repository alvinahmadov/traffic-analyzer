#include <fmt/format.h>

#include "utils.hpp"
#include "layers/slice.hpp"

bool sliceLayer(nvinfer1::ITensor *&output, int index, const std::string &name, nvinfer1::ITensor *input,
								nvinfer1::Dims start, nvinfer1::Dims size, nvinfer1::Dims stride, nvinfer1::INetworkDefinition *network,
								uint batch_size)
{
	bool success{};
	std::string layer_name;

	int tensorBatch = input->getDimensions().d[0];

	nvinfer1::ISliceLayer *slice = network->addSlice(*input, start, size, stride);

	if(tensorBatch == -1)
	{
		int nb_dims = size.nbDims;
		nvinfer1::IElementWiseLayer *new_size;
		nvinfer1::IConstantLayer *constant1;
		nvinfer1::Weights constant1_wt{ nvinfer1::DataType::kINT32, nullptr, nb_dims },
				constant2_wt{ nvinfer1::DataType::kINT32, nullptr, nb_dims };
		nvinfer1::ITensor *constant1_tensor, *constant2_tensor, *new_size_tensor;

		int *val1 = new int[nb_dims];
		val1[0] = 1;
		for(int i = 1; i < nb_dims; ++i)
		{
			val1[i] = size.d[i];
		}
		constant1_wt.values = val1;
		constant1 = network->addConstant(nvinfer1::Dims{ 1, { nb_dims } }, constant1_wt);
		layer_name = fmt::format("constant1_{}_{}", name, index);

		if(!constant1)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		constant1->setName(layer_name.c_str());
		constant1_tensor = constant1->getOutput(0);

		int *val2 = new int[nb_dims];
		val2[0] = batch_size;
		for(int i = 1; i < nb_dims; ++i)
		{
			val2[i] = 1;
		}
		constant2_wt.values = val2;

		nvinfer1::IConstantLayer *constant2 = network->addConstant(nvinfer1::Dims{ 1, { nb_dims } }, constant2_wt);
		layer_name = fmt::format("constant2_{}_{}", name, index);

		if(!constant2)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		constant2->setName(layer_name.c_str());
		constant2_tensor = constant2->getOutput(0);

		new_size = network->addElementWise(*constant1_tensor, *constant2_tensor, nvinfer1::ElementWiseOperation::kPROD);
		layer_name = fmt::format("new_size_{}_{}", name, index);

		if(!new_size)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		new_size->setName(layer_name.c_str());
		new_size_tensor = new_size->getOutput(0);

		slice->setInput(2, *new_size_tensor);
	}

	layer_name = fmt::format("{}_{}", name, index);

	if(!slice)
	{
		TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
		goto done;
	}

	slice->setName(layer_name.c_str());
	output = slice->getOutput(0);

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}