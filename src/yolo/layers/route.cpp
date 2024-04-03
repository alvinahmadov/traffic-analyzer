#include <fmt/format.h>

#include "utils.hpp"
#include "layers/route.hpp"

bool routeLayer(nvinfer1::ITensor *&output, int index, std::string &layers,
								const std::map<std::string, std::string> &block, std::vector<nvinfer1::ITensor *> tensor_outputs,
								nvinfer1::INetworkDefinition *network, uint batch_size)
{
	bool success{};
	std::string_view block_type{ block.at("type") };

	std::string layer_name;
	std::vector<nvinfer1::ITensor *> concat_inputs;
	std::string str_layers;
	std::vector<int> idx_layers;
	size_t pos, last_pos{};

	if(block_type != "route")
	{
		TADS_ERR_MSG_V("Block type '%s' doesn't match 'route'", block_type.data());
		goto done;
	}
	if(block.find("layers") == block.end())
	{
		TADS_ERR_MSG_V("Layers not found");
		goto done;
	}

	str_layers = block.at("layers");
	while((pos = str_layers.find(',', last_pos)) != std::string::npos)
	{
		int vL = std::stoi(trim1(str_layers.substr(last_pos, pos - last_pos)));
		idx_layers.push_back(vL);
		last_pos = pos + 1;
	}
	if(last_pos < str_layers.length())
	{
		std::string lastV = trim1(str_layers.substr(last_pos));
		if(!lastV.empty())
		{
			idx_layers.push_back(std::stoi(lastV));
		}
	}
	if(idx_layers.empty())
	{
		goto done;
	}
	for(uint i = 0; i < idx_layers.size(); ++i)
	{
		if(idx_layers[i] < 0)
		{
			idx_layers[i] = tensor_outputs.size() + idx_layers[i];
		}
		if(idx_layers[i] <= 0 && idx_layers[i] > static_cast<int>(tensor_outputs.size()))
		{
			goto done;
		}

		concat_inputs.push_back(tensor_outputs[idx_layers[i]]);
		if(i < idx_layers.size() - 1)
		{
			layers.append(std::to_string(idx_layers[i]) + ", ");
		}
	}
	layers.append(std::to_string(idx_layers[idx_layers.size() - 1]));

	if(concat_inputs.size() == 1)
	{
		output = concat_inputs.at(0);
	}
	else
	{
		int axis = 1;
		if(block.find("axis") != block.end())
		{
			axis += std::stoi(block.at("axis"));
			printf("%d", axis);
		}
		if(axis < 0)
		{
			axis += concat_inputs[0]->getDimensions().nbDims;
		}

		nvinfer1::IConcatenationLayer *concat = network->addConcatenation(concat_inputs.data(), concat_inputs.size());
		layer_name = fmt::format("route_{}", index);

		if(!concat)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		concat->setName(layer_name.c_str());
		concat->setAxis(axis);
		output = concat->getOutput(0);
	}

	if(block.find("groups") != block.end())
	{
		nvinfer1::Dims prev_tensor_dims = output->getDimensions();
		int groups = stoi(block.at("groups"));
		int group_id = stoi(block.at("group_id"));
		int start_slice = (prev_tensor_dims.d[1] / groups) * group_id;
		int channel_slice = (prev_tensor_dims.d[1] / groups);

		std::string name{ "slice" };
		nvinfer1::Dims start{
			4, { 0, start_slice, 0, 0 }
		};
		nvinfer1::Dims size{
			4, { prev_tensor_dims.d[0], channel_slice, prev_tensor_dims.d[2], prev_tensor_dims.d[3] }
		};
		nvinfer1::Dims stride{
			4, { 1, 1, 1, 1 }
		};

		if(!sliceLayer(output, index, name, output, start, size, stride, network, batch_size))
		{
			goto done;
		}
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}