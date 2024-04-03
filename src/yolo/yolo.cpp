#include <cassert>
#include <fstream>

#include <fmt/format.h>

#include "NvOnnxParser.h"

#include "utils.hpp"
#include "yolo.hpp"
#include "plugins.hpp"

#ifdef OPENCV
#include "calibrator.hpp"
#endif

Yolo::Yolo(const NetworkInfo &network_info):
	m_input_blob_name{ network_info.input_blob_name },
	m_network_type{ network_info.network_type },
	m_model_name{ network_info.model_name },
	m_onnx_wts_file_path{ network_info.onnx_wts_file_path },
	m_darknet_wts_file_path{ network_info.darknet_wts_file_path },
	m_darknet_cfg_file_path{ network_info.darknet_cfg_file_path },
	m_batch_size{ network_info.batch_size },
	m_implicit_batch{ network_info.implicit_batch },
	m_int8_calib_path{ network_info.int8_calib_path },
	m_device_type{ network_info.device_type },
	m_num_detected_classes{ network_info.num_detected_classes },
	m_cluster_mode{ network_info.cluster_mode },
	m_network_mode{ network_info.network_mode },
	m_scale_factor{ network_info.scale_factor },
	m_offsets{ network_info.offsets },
	m_workspace_size{ network_info.workspace_size },
	m_input_c{},
	m_input_h{},
	m_input_w{},
	m_input_size{},
	m_num_classes{},
	m_letter_box{},
	m_new_coords{},
	m_yolo_count{}
{}

Yolo::~Yolo()
{
	destroyNetworkUtils();
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
nvinfer1::ICudaEngine *
#if NV_TENSORRT_MAJOR >= 8
Yolo::createEngine(nvinfer1::IBuilder *builder, nvinfer1::IBuilderConfig *config)
#else
Yolo::createEngine(nvinfer1::IBuilder *builder)
#endif

{
	assert(builder);

#if NV_TENSORRT_MAJOR < 8
	nvinfer1::IBuilderConfig *config = builder->createBuilderConfig();
	if(m_WorkspaceSize > 0)
	{
		config->setMaxWorkspaceSize((size_t)m_WorkspaceSize * 1024 * 1024);
	}
#endif

	nvinfer1::NetworkDefinitionCreationFlags flags =
			1U << static_cast<uint32_t>(nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);

	nvinfer1::INetworkDefinition *network = builder->createNetworkV2(flags);
	assert(network);

	nvonnxparser::IParser *parser;

	if(m_network_type == "onnx")
	{

#if NV_TENSORRT_MAJOR >= 8 && NV_TENSORRT_MINOR > 0
		parser = nvonnxparser::createParser(*network, *builder->getLogger());
#else
		parser = nvonnxparser::createParser(*network, logger);
#endif

		if(!parser->parseFromFile(m_onnx_wts_file_path.c_str(), static_cast<INT>(nvinfer1::ILogger::Severity::kWARNING)))
		{
			TADS_ERR_MSG_V("Could not parse the ONNX model");

#if NV_TENSORRT_MAJOR >= 8
			delete parser;
			delete network;
#else
			parser->destroy();
			config->destroy();
			network->destroy();
#endif

			return nullptr;
		}
		m_input_c = network->getInput(0)->getDimensions().d[1];
		m_input_h = network->getInput(0)->getDimensions().d[2];
		m_input_w = network->getInput(0)->getDimensions().d[3];
	}
	else
	{
		m_config_blocks = parseConfigFile(m_darknet_cfg_file_path);
		parseConfigBlocks();
		if(parseModel(*network) != NVDSINFER_SUCCESS)
		{

#if NV_TENSORRT_MAJOR >= 8
			delete network;
#else
			config->destroy();
			network->destroy();
#endif

			return nullptr;
		}
	}

	if((m_network_type == "darknet" && !m_implicit_batch) || network->getInput(0)->getDimensions().d[0] == -1)
	{
		nvinfer1::IOptimizationProfile *profile = builder->createOptimizationProfile();
		assert(profile);
		for(INT i = 0; i < network->getNbInputs(); ++i)
		{
			nvinfer1::ITensor *input = network->getInput(i);
			nvinfer1::Dims inputDims = input->getDimensions();
			nvinfer1::Dims dims = inputDims;
			dims.d[0] = 1;
			profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMIN, dims);
			dims.d[0] = m_batch_size;
			profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kOPT, dims);
			dims.d[0] = m_batch_size;
			profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMAX, dims);
		}
		config->addOptimizationProfile(profile);
	}

	if(m_network_type == "darknet")
	{
		if(m_num_classes != m_num_detected_classes)
		{
			TADS_WARN_MSG_V("Number of classes mismatch, make sure to set num-detected-classes=%d in config_infer file",
											m_num_classes);
		}
		if(m_letter_box == 1)
		{
			TADS_WARN_MSG_V("letter_box is set in cfg file, make sure to set maintain-aspect-ratio=1 in config_infer file to "
											"get better accuracy");
		}
	}
	if(m_cluster_mode != 2)
	{
		TADS_WARN_MSG_V("Wrong cluster-mode is set, make sure to set cluster-mode=2 in config_infer file");
	}

	if(m_network_mode == "FP16")
	{
		assert(builder->platformHasFastFp16());
		config->setFlag(nvinfer1::BuilderFlag::kFP16);
	}
	else if(m_network_mode == "INT8")
	{
		assert(builder->platformHasFastInt8());
		config->setFlag(nvinfer1::BuilderFlag::kINT8);
		if(!m_int8_calib_path.empty() && !file_exists(m_int8_calib_path))
		{

#ifdef OPENCV
			std::string calib_image_list;
			int calib_batch_size;
			if(getenv("INT8_CALIB_IMG_PATH"))
			{
				calib_image_list = getenv("INT8_CALIB_IMG_PATH");
			}
			else
			{
				TADS_ERR_MSG_V("INT8_CALIB_IMG_PATH not set");
				assert(false);
			}
			if(getenv("INT8_CALIB_BATCH_SIZE"))
			{
				calib_batch_size = std::stoi(getenv("INT8_CALIB_BATCH_SIZE"));
			}
			else
			{
				std::cerr << "INT8_CALIB_BATCH_SIZE not set" << std::endl;
				assert(false);
			}
			nvinfer1::IInt8EntropyCalibrator2 *calibrator =
					new Int8EntropyCalibrator2(calib_batch_size, m_input_c, m_input_h, m_input_w, m_scale_factor, m_offsets,
																		 calib_image_list, m_int8_calib_path);
			config->setInt8Calibrator(calibrator);
#else
			std::cerr << "OpenCV is required to run INT8 calibrator\n" << std::endl;

#if NV_TENSORRT_MAJOR >= 8
			if(m_network_type == "onnx")
			{
				delete parser;
			}
			delete network;
#else
			if(m_NetworkType == "onnx")
			{
				parser->destroy();
			}
			config->destroy();
			network->destroy();
#endif

			return nullptr;
#endif
		}
	}

#ifdef GRAPH
	config->setProfilingVerbosity(nvinfer1::ProfilingVerbosity::kDETAILED);
#endif

	nvinfer1::ICudaEngine *engine = builder->buildEngineWithConfig(*network, *config);

#ifdef GRAPH
	nvinfer1::IExecutionContext *context = engine->createExecutionContext();
	nvinfer1::IEngineInspector *inpector = engine->createEngineInspector();
	inpector->setExecutionContext(context);
	std::ofstream graph;
	graph.open("graph.json");
	graph << inpector->getEngineInformation(nvinfer1::LayerInformationFormat::kJSON);
	graph.close();
	std::cout << "Network graph saved to graph.json\n" << std::endl;

#if NV_TENSORRT_MAJOR >= 8
	delete inpector;
	delete context;
#else
	inpector->destroy();
	context->destroy();
#endif

#endif

#if NV_TENSORRT_MAJOR >= 8
	if(m_network_type == "onnx")
	{
		delete parser;
	}
	delete network;
#else
	if(m_NetworkType == "onnx")
	{
		parser->destroy();
	}
	config->destroy();
	network->destroy();
#endif

	return engine;
}
#pragma clang diagnostic pop

NvDsInferStatus Yolo::parseModel(nvinfer1::INetworkDefinition &network)
{
	destroyNetworkUtils();

	std::vector<float> weights = load_weights(m_darknet_wts_file_path, m_model_name);
	NvDsInferStatus status = buildYoloNetwork(weights, network);

	if(status == NVDSINFER_SUCCESS)
	{
		TADS_DBG_MSG_V("Building YOLO network complete");
	}
	else
	{
		TADS_ERR_MSG_V("Building YOLO network failed");
	}

	return status;
}

NvDsInferStatus Yolo::buildYoloNetwork(std::vector<float> &weights, nvinfer1::INetworkDefinition &network)
{
	int weight_ptr{};
	uint yolo_count_inputs{};
	NvDsInferStatus status{ NVDSINFER_SUCCESS };

	std::string layer_name;
	std::string layer_index;
	std::string input_vol, output_vol;
	std::string activation;

	nvinfer1::ITensor *previous;
	std::vector<nvinfer1::ITensor *> tensor_outputs;
	nvinfer1::ITensor *yolo_tensor_inputs[m_yolo_count];

	uint batch_size = m_implicit_batch ? m_batch_size : -1;

	nvinfer1::ITensor *data = network.addInput(m_input_blob_name.c_str(), nvinfer1::DataType::kFLOAT,
																						 nvinfer1::Dims{
																								 4,
																								 { static_cast<int>(batch_size), static_cast<int>(m_input_c),
																									 static_cast<int>(m_input_h), static_cast<int>(m_input_w) }
	});
	if(!data || data->getDimensions().nbDims <= 0)
	{
		// TODO Add goto
		goto done;
	}

	previous = data;

	for(uint i = 0; i < m_config_blocks.size(); ++i)
	{
		const ConfigBlock &block{ m_config_blocks.at(i) };
		std::string_view block_type{ block.at("type") };

		layer_index = fmt::format("({})", tensor_outputs.size());
		if(block_type == "net")
			print_layer_info("", "Layer", "Input Shape", "Output Shape", "WeightPtr");
		else if(block_type == "conv" || block_type == "convolutional")
		{
			int channels = get_num_channels(previous);
			input_vol = dims_to_string(previous->getDimensions());

			if(!convolutionalLayer(previous, i, block, weights, m_trt_weights, weight_ptr, channels, previous, &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("conv_{}", block.at("activation"));
			print_layer_info(layer_index, layer_name, input_vol, output_vol, std::to_string(weight_ptr));
		}
		else if(block_type == "deconvolutional")
		{
			int channels = get_num_channels(previous);
			input_vol = dims_to_string(previous->getDimensions());

			if(!deconvolutionalLayer(previous, i, block, weights, m_trt_weights, weight_ptr, channels, previous, &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = "deconv";
			print_layer_info(layer_index, layer_name, input_vol, output_vol, std::to_string(weight_ptr));
		}
		else if(block_type == "batchnorm")
		{
			input_vol = dims_to_string(previous->getDimensions());

			if(!batchnormLayer(previous, i, block, weights, m_trt_weights, weight_ptr, previous, &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("batchnorm_{}", block.at("activation"));
			print_layer_info(layer_index, layer_name, input_vol, output_vol, std::to_string(weight_ptr));
		}
		else if(block_type == "implicit" || block_type == "implicit_add" || block_type == "implicit_mul")
		{
			if(!implicitLayer(previous, i, block, weights, m_trt_weights, weight_ptr, &network))
			{
				goto done;
			}

			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = "implicit";
			print_layer_info(layer_index, layer_name, "-", output_vol, std::to_string(weight_ptr));
		}
		else if(block_type == "shift_channels" || block_type == "control_channels")
		{
			assert(block.find("from") != block.end());
			int from = stoi(block.at("from"));
			if(from > 0)
				from = from - i + 1;
			assert((i - 2 >= 0) && (i - 2 < tensor_outputs.size()));
			assert((i + from - 1 >= 0) && (i + from - 1 < tensor_outputs.size()));
			assert(i + from - 1 < i - 2);

			input_vol = dims_to_string(previous->getDimensions());

			if(!channelsLayer(previous, i, block, previous, tensor_outputs[i + from - 1], &network))
			{
				goto done;
			}

			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("{}: {}", block_type, i + from - 1);
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");
		}
		else if(block_type == "shortcut")
		{
			assert(block.find("from") != block.end());
			int from = stoi(block.at("from"));
			if(from > 0)
				from = from - i + 1;
			assert((i - 2 >= 0) && (i - 2 < tensor_outputs.size()));
			assert((i + from - 1 >= 0) && (i + from - 1 < tensor_outputs.size()));
			assert(i + from - 1 < i - 2);

			activation = "linear";
			if(block.find("activation") != block.end())
				activation = block.at("activation");

			input_vol = dims_to_string(previous->getDimensions());
			std::string shortcutVol = dims_to_string(tensor_outputs[i + from - 1]->getDimensions());
			if(!shortcutLayer(previous, i, activation, input_vol, shortcutVol, block, previous, tensor_outputs[i + from - 1],
												&network, m_batch_size))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("shortcut_{}: {}", activation, i + from - 1);
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");

			if(input_vol != shortcutVol)
				std::cout << input_vol << " +" << shortcutVol << std::endl;
		}
		else if(block_type == "sam")
		{
			assert(block.find("from") != block.end());
			int from = stoi(block.at("from"));
			if(from > 0)
				from = from - i + 1;
			assert((i - 2 >= 0) && (i - 2 < tensor_outputs.size()));
			assert((i + from - 1 >= 0) && (i + from - 1 < tensor_outputs.size()));
			assert(i + from - 1 < i - 2);

			activation = "linear";
			if(block.find("activation") != block.end())
				activation = block.at("activation");

			input_vol = dims_to_string(previous->getDimensions());
			if(!samLayer(previous, i, activation, block, previous, tensor_outputs[i + from - 1], &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("sam_{}: {}", activation, i + from - 1);
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");
		}
		else if(block_type == "route")
		{
			std::string layers;
			if(!routeLayer(previous, i, layers, block, tensor_outputs, &network, m_batch_size))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = fmt::format("route: {}", layers);
			print_layer_info(layer_index, layer_name, "-", output_vol, "-");
		}
		else if(block_type == "upsample")
		{
			input_vol = dims_to_string(previous->getDimensions());
			if(!upsampleLayer(previous, i, block, previous, &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = "upsample";
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");
		}
		else if(block_type == "max" || block_type == "maxpool" || block_type == "avg" || block_type == "avgpool")
		{
			input_vol = dims_to_string(previous->getDimensions());

			if(!poolingLayer(previous, i, block, previous, &network))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = block.at("type");
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");
		}
		else if(block_type == "reorg" || block_type == "reorg3d")
		{
			input_vol = dims_to_string(previous->getDimensions());
			if(!reorgLayer(previous, i, block, previous, &network, m_batch_size))
			{
				goto done;
			}
			output_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			layer_name = block.at("type");
			print_layer_info(layer_index, layer_name, input_vol, output_vol, "-");
		}
		else if(block_type == "yolo" || block_type == "region")
		{
			std::string blob_name = block_type == "yolo" ? fmt::format("yolo_{}", i) : fmt::format("region_{}", i);
			nvinfer1::Dims prev_tensor_dims = previous->getDimensions();
			TensorInfo &cur_yolo_tensor = m_yolo_tensors.at(yolo_count_inputs);
			cur_yolo_tensor.blob_name = blob_name;
			cur_yolo_tensor.grid_size_y = prev_tensor_dims.d[2];
			cur_yolo_tensor.grid_size_x = prev_tensor_dims.d[3];
			input_vol = dims_to_string(previous->getDimensions());
			tensor_outputs.push_back(previous);
			yolo_tensor_inputs[yolo_count_inputs] = previous;
			++yolo_count_inputs;
			layer_name = block_type == "yolo" ? "yolo" : "region";
			print_layer_info(layer_index, layer_name, input_vol, "-", "-");
		}
		else if(block_type == "dropout")
		{
			continue;
		}
		else
		{
			TADS_ERR_MSG_V("Unsupported layer type '%s'", block_type.data());
			status = NVDSINFER_INVALID_PARAMS;
			goto done;
		}
	}

	if(static_cast<int>(weights.size()) != weight_ptr)
	{
		TADS_ERR_MSG_V("Number of unused weights left: %d", static_cast<int>(weights.size() - weight_ptr));
		status = NVDSINFER_INVALID_PARAMS;
		goto done;
	}

	if(m_yolo_count == yolo_count_inputs)
	{
		uint64_t output_size = 0;
		for(uint j = 0; j < yolo_count_inputs; ++j)
		{
			TensorInfo &cur_yolo_tensor = m_yolo_tensors.at(j);
			output_size += cur_yolo_tensor.num_bboxes * cur_yolo_tensor.grid_size_y * cur_yolo_tensor.grid_size_x;
		}

		nvinfer1::IPluginV2DynamicExt *yolo_plugin =
				new YoloLayer(m_input_w, m_input_h, m_num_classes, m_new_coords, m_yolo_tensors, output_size);
		nvinfer1::IPluginV2Layer *yolo = network.addPluginV2(yolo_tensor_inputs, m_yolo_count, *yolo_plugin);
		layer_name = "yolo";

		if(!yolo)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		yolo->setName(layer_name.c_str());
		std::string output_layer_name;
		nvinfer1::ITensor *detection_boxes = yolo->getOutput(0);
		output_layer_name = "boxes";
		detection_boxes->setName(output_layer_name.c_str());

		nvinfer1::ITensor *detection_scores = yolo->getOutput(1);
		output_layer_name = "scores";
		detection_scores->setName(output_layer_name.c_str());

		nvinfer1::ITensor *detection_classes = yolo->getOutput(2);
		output_layer_name = "classes";
		detection_classes->setName(output_layer_name.c_str());

		network.markOutput(*detection_boxes);
		network.markOutput(*detection_scores);
		network.markOutput(*detection_classes);
	}
	else
	{
		TADS_ERR_MSG_V("Error in yolo cfg file");
		status = NvDsInferStatus::NVDSINFER_CONFIG_FAILED;
		goto done;
	}
done:

	if(status != NvDsInferStatus::NVDSINFER_SUCCESS)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}

	return status;
}

std::vector<std::map<std::string, std::string>>
Yolo::parseConfigFile(const std::string &cfg_file_path) // NOLINT(*-convert-member-functions-to-static)
{
	assert(file_exists(cfg_file_path));
	std::ifstream file(cfg_file_path);
	assert(file.good());
	std::string line;
	std::vector<std::map<std::string, std::string>> blocks;
	std::map<std::string, std::string> block;

	while(getline(file, line))
	{
		if(line.empty() || line.front() == ' ' || line.front() == '#')
			continue;

		line = trim1(line);
		if(line.front() == '[')
		{
			if(!block.empty())
			{
				blocks.push_back(block);
				block.clear();
			}
			std::string key = "type";
			std::string value = trim1(line.substr(1, line.size() - 2));
			block.insert(std::pair<std::string, std::string>(key, value));
		}
		else
		{
			int cpos = line.find('=');
			std::string key = trim1(line.substr(0, cpos));
			std::string value = trim1(line.substr(cpos + 1));
			block.insert(std::pair<std::string, std::string>(key, value));
		}
	}

	blocks.push_back(block);
	return blocks;
}

void Yolo::parseConfigBlocks()
{
	for(auto block : m_config_blocks)
	{
		if(block.at("type") == "net")
		{
			assert((block.find("height") != block.end()) && "Missing 'height' param in network cfg");
			assert((block.find("width") != block.end()) && "Missing 'width' param in network cfg");
			assert((block.find("channels") != block.end()) && "Missing 'channels' param in network cfg");

			m_input_h = std::stoul(block.at("height"));
			m_input_w = std::stoul(block.at("width"));
			m_input_c = std::stoul(block.at("channels"));
			m_input_size = m_input_c * m_input_h * m_input_w;

			if(block.find("letter_box") != block.end())
				m_letter_box = std::stoul(block.at("letter_box"));
		}
		else if((block.at("type") == "region") || (block.at("type") == "yolo"))
		{
			assert((block.find("num") != block.end()) &&
						 std::string("Missing 'num' param in " + block.at("type") + " layer").c_str());
			assert((block.find("classes") != block.end()) &&
						 std::string("Missing 'classes' param in " + block.at("type") + " layer").c_str());
			assert((block.find("anchors") != block.end()) &&
						 std::string("Missing 'anchors' param in " + block.at("type") + " layer").c_str());

			++m_yolo_count;

			m_num_classes = std::stoul(block.at("classes"));

			if(block.find("new_coords") != block.end())
				m_new_coords = std::stoul(block.at("new_coords"));

			TensorInfo output_tensor;

			std::string anchor_string = block.at("anchors");
			while(!anchor_string.empty())
			{
				int npos = anchor_string.find_first_of(',');
				if(npos != -1)
				{
					float anchor = std::stof(trim1(anchor_string.substr(0, npos)));
					output_tensor.anchors.push_back(anchor);
					anchor_string.erase(0, npos + 1);
				}
				else
				{
					float anchor = std::stof(trim1(anchor_string));
					output_tensor.anchors.push_back(anchor);
					break;
				}
			}

			if(block.find("mask") != block.end())
			{
				std::string mask_string = block.at("mask");
				while(!mask_string.empty())
				{
					int npos = mask_string.find_first_of(',');
					if(npos != -1)
					{
						int mask = std::stoul(trim1(mask_string.substr(0, npos)));
						output_tensor.mask.push_back(mask);
						mask_string.erase(0, npos + 1);
					}
					else
					{
						int mask = std::stoul(trim1(mask_string));
						output_tensor.mask.push_back(mask);
						break;
					}
				}
			}

			if(block.find("scale_x_y") != block.end())
				output_tensor.scale_xy = std::stof(block.at("scale_x_y"));
			else
				output_tensor.scale_xy = 1.0;

			output_tensor.num_bboxes =
					!output_tensor.mask.empty() ? output_tensor.mask.size() : std::stoul(trim1(block.at("num")));

			m_yolo_tensors.push_back(output_tensor);
		}
	}
}

void Yolo::destroyNetworkUtils()
{
	for(auto &m_TrtWeight : m_trt_weights)
		if(m_TrtWeight.count > 0)
			free(const_cast<void *>(m_TrtWeight.values));
	m_trt_weights.clear();
}