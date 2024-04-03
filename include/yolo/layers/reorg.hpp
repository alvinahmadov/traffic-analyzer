#ifndef TADS_REORG_HPP
#define TADS_REORG_HPP

#include <map>
#include <string>
#include <NvInfer.h>

#include "slice.hpp"

bool reorgLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
								nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network, uint batch_size);

#endif // TADS_REORG_HPP
