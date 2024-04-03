#ifndef TADS_SLICE_HPP
#define TADS_SLICE_HPP

#include <string>

#include <NvInfer.h>

bool sliceLayer(nvinfer1::ITensor *&output, int index, const std::string &name, nvinfer1::ITensor *input,
								 nvinfer1::Dims start, nvinfer1::Dims size, nvinfer1::Dims stride,
								 nvinfer1::INetworkDefinition *network, uint batch_size);

#endif // TADS_SLICE_HPP
