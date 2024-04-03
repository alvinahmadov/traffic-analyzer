#ifndef TADS_UPSAMPLE_HPP
#define TADS_UPSAMPLE_HPP

#include <map>
#include <string>

#include <NvInfer.h>

bool upsampleLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network);

#endif // TADS_UPSAMPLE_HPP
