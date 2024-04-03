#ifndef TADS_CONVOLUTIONAL_HPP
#define TADS_CONVOLUTIONAL_HPP

#include <map>
#include <vector>

#include <NvInfer.h>

#include "layers/activation.hpp"

bool convolutionalLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
												std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
												int &input_channels, nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network,
												const std::string& name = "");

#endif // TADS_CONVOLUTIONAL_HPP
