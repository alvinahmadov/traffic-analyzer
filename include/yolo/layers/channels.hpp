#ifndef TADS_CHANNELS_HPP
#define TADS_CHANNELS_HPP

#include <map>
#include <string>

#include <nvdsinfer.h>

bool channelsLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 nvinfer1::ITensor *input, nvinfer1::ITensor *implicit_tensor, nvinfer1::INetworkDefinition *network);

#endif // TADS_CHANNELS_HPP
