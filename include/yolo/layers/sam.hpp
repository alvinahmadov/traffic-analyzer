#ifndef TADS_SAM_HPP
#define TADS_SAM_HPP

#include <map>

#include <NvInfer.h>

#include "layers/activation.hpp"

bool samLayer(nvinfer1::ITensor *&output, int index, std::string_view activation,
							const std::map<std::string, std::string> &block, nvinfer1::ITensor *input, nvinfer1::ITensor *sam_input,
							nvinfer1::INetworkDefinition *network);

#endif // TADS_SAM_HPP
