#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-avoid-const-params-in-decls"

#include <algorithm>
#include <iostream>

#include "nvdsinfer_custom_impl.h"
#include "nvdsinfer_context.h"

#include "yolo.hpp"

#define USE_CUDA_ENGINE_GET_API 1

static bool getYoloNetworkInfo(NetworkInfo &networkInfo, const NvDsInferContextInitParams *initParams)
{
	std::string onnx_wts_file_path = initParams->onnxFilePath;
	std::string darknet_wts_file_path = initParams->modelFilePath;
	std::string darknet_cfg_file_path = initParams->customNetworkConfigFilePath;

	std::string yolo_type = !onnx_wts_file_path.empty() ? "onnx" : "darknet";
	std::string model_name =
			yolo_type == "onnx"
					? onnx_wts_file_path.substr(0, onnx_wts_file_path.find(".onnx")).substr(onnx_wts_file_path.rfind('/') + 1)
					: darknet_wts_file_path.substr(0, darknet_wts_file_path.find(".weights")).substr(darknet_wts_file_path.rfind('/') + 1);

	std::transform(model_name.begin(), model_name.end(), model_name.begin(), [](uint8_t c) { return std::tolower(c); });

	networkInfo.input_blob_name = "input";
	networkInfo.network_type = yolo_type;
	networkInfo.model_name = model_name;
	networkInfo.onnx_wts_file_path = onnx_wts_file_path;
	networkInfo.darknet_wts_file_path = darknet_wts_file_path;
	networkInfo.darknet_cfg_file_path = darknet_cfg_file_path;
	networkInfo.batch_size = initParams->maxBatchSize;
	networkInfo.implicit_batch = initParams->forceImplicitBatchDimension;
	networkInfo.int8_calib_path = initParams->int8CalibrationFilePath;
	networkInfo.device_type = initParams->useDLA ? "kDLA" : "kGPU";
	networkInfo.num_detected_classes = initParams->numDetectedClasses;
	networkInfo.cluster_mode = initParams->clusterMode;
	networkInfo.scale_factor = initParams->networkScaleFactor;
	networkInfo.offsets = initParams->offsets;
	networkInfo.workspace_size = initParams->workspaceSize;

	if(initParams->networkMode == NvDsInferNetworkMode_FP32)
		networkInfo.network_mode = "FP32";
	else if(initParams->networkMode == NvDsInferNetworkMode_INT8)
		networkInfo.network_mode = "INT8";
	else if(initParams->networkMode == NvDsInferNetworkMode_FP16)
		networkInfo.network_mode = "FP16";

	if(yolo_type == "onnx")
	{
		if(!file_exists(networkInfo.onnx_wts_file_path))
		{
			std::cerr << "ONNX model file does not exist\n" << std::endl;
			return false;
		}
	}
	else
	{
		if(!file_exists(networkInfo.darknet_wts_file_path))
		{
			std::cerr << "Darknet weights file does not exist\n" << std::endl;
			return false;
		}
		else if(!file_exists(networkInfo.darknet_cfg_file_path))
		{
			std::cerr << "Darknet cfg file does not exist\n" << std::endl;
			return false;
		}
	}

	return true;
}

#if !USE_CUDA_ENGINE_GET_API
IModelParser *NvDsInferCreateModelParser(const NvDsInferContextInitParams *initParams)
{
	NetworkInfo networkInfo;
	if(!getYoloNetworkInfo(networkInfo, initParams))
		return nullptr;

	return new Yolo(networkInfo);
}
#else

#if NV_TENSORRT_MAJOR >= 8
extern "C" bool
NvDsInferYoloCudaEngineGet(nvinfer1::IBuilder *const builder, nvinfer1::IBuilderConfig *const builderConfig,
													 const NvDsInferContextInitParams *const initParams,
													 [[maybe_unused]] nvinfer1::DataType dataType, nvinfer1::ICudaEngine *&cudaEngine);

extern "C" bool
NvDsInferYoloCudaEngineGet(nvinfer1::IBuilder *const builder, nvinfer1::IBuilderConfig *const builderConfig,
													 const NvDsInferContextInitParams *const initParams,
													 [[maybe_unused]] nvinfer1::DataType dataType, nvinfer1::ICudaEngine *&cudaEngine)
#else
extern "C" bool NvDsInferYoloCudaEngineGet(nvinfer1::IBuilder *const builder,
																					 const NvDsInferContextInitParams *const initParams,
																					 nvinfer1::DataType dataType, nvinfer1::ICudaEngine *&cudaEngine);

extern "C" bool NvDsInferYoloCudaEngineGet(nvinfer1::IBuilder *const builder,
																					 const NvDsInferContextInitParams *const initParams,
																					 nvinfer1::DataType dataType, nvinfer1::ICudaEngine *&cudaEngine)
#endif

{
	NetworkInfo network_info;
	if(!getYoloNetworkInfo(network_info, initParams))
		return false;

	Yolo yolo(network_info);

#if NV_TENSORRT_MAJOR >= 8
	cudaEngine = yolo.createEngine(builder, builderConfig);
#else
	cudaEngine = yolo.createEngine(builder);
#endif

	if(cudaEngine == nullptr)
	{
		std::cerr << "Failed to build CUDA engine" << std::endl;
		return false;
	}

	return true;
}
#endif
#pragma clang diagnostic pop