#ifndef TADS_DECONVOLUTIONAL_HPP
#define TADS_DECONVOLUTIONAL_HPP

#include <map>
#include <vector>
#include <string>

#include <NvInfer.h>

bool deconvolutionalLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
													std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
													int &input_channels, nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network,
													const std::string &name = "");

#endif // TADS_DECONVOLUTIONAL_HPP
