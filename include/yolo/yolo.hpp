#ifndef TADS_YOLO_HPP
#define TADS_YOLO_HPP

#include <NvInferPlugin.h>
#include <nvdsinfer_custom_impl.h>

#include "utils.hpp"

#include "layers/convolutional.hpp"
#include "layers/deconvolutional.hpp"
#include "layers/batchnorm.hpp"
#include "layers/implicit.hpp"
#include "layers/channels.hpp"
#include "layers/shortcut.hpp"
#include "layers/sam.hpp"
#include "layers/route.hpp"
#include "layers/upsample.hpp"
#include "layers/pooling.hpp"
#include "layers/reorg.hpp"

#if NV_TENSORRT_MAJOR >= 8
#define INT int32_t
#else
#define INT int
#endif

#if NV_TENSORRT_MAJOR < 8 || (NV_TENSORRT_MAJOR == 8 && NV_TENSORRT_MINOR == 0)
static class Logger : public nvinfer1::ILogger
{
	void log(nvinfer1::ILogger::Severity severity, const char *msg) noexcept override
	{
		if(severity <= nvinfer1::ILogger::Severity::kWARNING)
			std::cout << msg << std::endl;
	}
} logger;
#endif

using ConfigBlock = std::map<std::string, std::string>;

struct NetworkInfo
{
	std::string input_blob_name;
	std::string network_type;
	std::string model_name;
	std::string onnx_wts_file_path;
	std::string darknet_wts_file_path;
	std::string darknet_cfg_file_path;
	uint batch_size;
	int implicit_batch;
	std::string int8_calib_path;
	std::string device_type;
	uint num_detected_classes;
	int cluster_mode;
	std::string network_mode;
	float scale_factor;
	const float *offsets;
	uint workspace_size;
};

struct TensorInfo
{
	std::string blob_name;
	uint grid_size_x{};
	uint grid_size_y{};
	uint num_bboxes{};
	float scale_xy;
	std::vector<float> anchors;
	std::vector<int> mask;
};

class Yolo : public IModelParser
{
public:
	explicit Yolo(const NetworkInfo &network_info);

	~Yolo() override;

	[[nodiscard]]
	bool hasFullDimsSupported() const override
	{
		return false;
	}

	[[nodiscard]]
	const char *getModelName() const override
	{
		return m_network_type == "onnx" ? m_onnx_wts_file_path.substr(0, m_onnx_wts_file_path.find(".onnx")).c_str()
																		: m_darknet_cfg_file_path.substr(0, m_darknet_cfg_file_path.find(".cfg")).c_str();
	}

	NvDsInferStatus parseModel(nvinfer1::INetworkDefinition &network) override;

#if NV_TENSORRT_MAJOR >= 8
	nvinfer1::ICudaEngine *createEngine(nvinfer1::IBuilder *builder, nvinfer1::IBuilderConfig *config);
#else
	nvinfer1::ICudaEngine *createEngine(nvinfer1::IBuilder *builder);
#endif

protected:
	const std::string m_input_blob_name;
	const std::string m_network_type;
	const std::string m_model_name;
	const std::string m_onnx_wts_file_path;
	const std::string m_darknet_wts_file_path;
	const std::string m_darknet_cfg_file_path;
	const uint m_batch_size;
	const int m_implicit_batch;
	const std::string m_int8_calib_path;
	const std::string m_device_type;
	const uint m_num_detected_classes;
	const int m_cluster_mode;
	const std::string m_network_mode;
	const float m_scale_factor;
	const float *m_offsets;
	const uint m_workspace_size;

	uint m_input_c;
	uint m_input_h;
	uint m_input_w;
	uint64_t m_input_size;
	uint m_num_classes;
	uint m_letter_box;
	uint m_new_coords;
	uint m_yolo_count;

	std::vector<TensorInfo> m_yolo_tensors;
	std::vector<ConfigBlock> m_config_blocks;
	std::vector<nvinfer1::Weights> m_trt_weights;

private:
	NvDsInferStatus buildYoloNetwork(std::vector<float> &weights, nvinfer1::INetworkDefinition &network);

	std::vector<std::map<std::string, std::string>> parseConfigFile(const std::string &cfg_file_path);

	void parseConfigBlocks();

	void destroyNetworkUtils();
};

#endif // TADS_YOLO_HPP