#ifndef TADS_PLUGINS_HPP
#define TADS_PLUGINS_HPP

#include <iostream>
#include <cuda_runtime_api.h>

#include "yolo.hpp"

namespace
{
const char *YOLOLAYER_PLUGIN_VERSION{ "1" };
const char *YOLOLAYER_PLUGIN_NAME{ "YoloLayer_TRT" };
} // namespace

class YoloLayer : public nvinfer1::IPluginV2DynamicExt
{
public:
	YoloLayer(const void *data, size_t length);

	YoloLayer(const uint &netWidth, const uint &netHeight, const uint &numClasses, const uint &newCoords,
						const std::vector<TensorInfo> &yoloTensors, const uint64_t &outputSize);

	[[nodiscard]]
	nvinfer1::IPluginV2DynamicExt *clone() const noexcept override;

	int initialize() noexcept override { return 0; }

	void terminate() noexcept override {}

	void destroy() noexcept override { delete this; }

	[[nodiscard]]
	size_t getSerializationSize() const noexcept override;

	void serialize(void *buffer) const noexcept override;

	[[nodiscard]]
	int getNbOutputs() const noexcept override
	{
		return 3;
	}

	nvinfer1::DimsExprs getOutputDimensions(INT index, const nvinfer1::DimsExprs *inputs, INT nbInputDims,
																					nvinfer1::IExprBuilder &exprBuilder) noexcept override;

	size_t getWorkspaceSize(const nvinfer1::PluginTensorDesc *inputs, INT nbInputs,
													const nvinfer1::PluginTensorDesc *outputs, INT nbOutputs) const noexcept override
	{
		return 0;
	}

	bool supportsFormatCombination(INT pos, const nvinfer1::PluginTensorDesc *inOut, INT nbInputs,
																 INT nbOutputs) noexcept override;

	[[nodiscard]]
	const char *getPluginType() const noexcept override
	{
		return YOLOLAYER_PLUGIN_NAME;
	}

	[[nodiscard]]
	const char *getPluginVersion() const noexcept override
	{
		return YOLOLAYER_PLUGIN_VERSION;
	}

	void setPluginNamespace(const char *pluginNamespace) noexcept override { m_Namespace = pluginNamespace; }

	[[nodiscard]]
	const char *getPluginNamespace() const noexcept override
	{
		return m_Namespace.c_str();
	}

	nvinfer1::DataType
	getOutputDataType(INT index, const nvinfer1::DataType *inputTypes, INT nbInputs) const noexcept override;

	void attachToContext(cudnnContext *cudnnContext, cublasContext *cublasContext,
											 nvinfer1::IGpuAllocator *gpuAllocator) noexcept override
	{}

	void configurePlugin(const nvinfer1::DynamicPluginTensorDesc *in, INT nbInput,
											 const nvinfer1::DynamicPluginTensorDesc *out, INT nbOutput) noexcept override;

	void detachFromContext() noexcept override {}

	INT enqueue(const nvinfer1::PluginTensorDesc *inputDesc, const nvinfer1::PluginTensorDesc *outputDesc,
							void const *const *inputs, void *const *outputs, void *workspace, cudaStream_t stream) noexcept override;

private:
	std::string m_Namespace;
	uint m_NetWidth{};
	uint m_NetHeight{};
	uint m_NumClasses{};
	uint m_NewCoords{};
	std::vector<TensorInfo> m_YoloTensors;
	uint64_t m_OutputSize{};
};

class YoloLayerPluginCreator : public nvinfer1::IPluginCreator
{
public:
	YoloLayerPluginCreator() = default;

	~YoloLayerPluginCreator() override = default;

	[[nodiscard]]
	const char *getPluginName() const noexcept override
	{
		return YOLOLAYER_PLUGIN_NAME;
	}

	[[nodiscard]]
	const char *getPluginVersion() const noexcept override
	{
		return YOLOLAYER_PLUGIN_VERSION;
	}

	const nvinfer1::PluginFieldCollection *getFieldNames() noexcept override
	{
		std::cerr << "YoloLayerPluginCreator::getFieldNames is not implemented" << std::endl;
		return nullptr;
	}

	nvinfer1::IPluginV2DynamicExt *
	createPlugin(const char *name, const nvinfer1::PluginFieldCollection *fc) noexcept override
	{
		std::cerr << "YoloLayerPluginCreator::getFieldNames is not implemented";
		return nullptr;
	}

	nvinfer1::IPluginV2DynamicExt *
	deserializePlugin(const char *name, const void *serialData, size_t serialLength) noexcept override
	{
		std::cout << "Deserialize yoloLayer plugin: " << name << std::endl;
		return new YoloLayer(serialData, serialLength);
	}

	void setPluginNamespace(const char *libNamespace) noexcept override { m_Namespace = libNamespace; }

	[[nodiscard]]
	const char *getPluginNamespace() const noexcept override
	{
		return m_Namespace.c_str();
	}

private:
	std::string m_Namespace{};
};

#endif // TADS_PLUGINS_HPP
