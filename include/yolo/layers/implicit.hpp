#ifndef TADS_IMPLICIT_HPP
#define TADS_IMPLICIT_HPP

#include <map>
#include <vector>
#include <string>

#include <NvInfer.h>

bool implicitLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
									 std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
									 nvinfer1::INetworkDefinition *network);

#endif // TADS_IMPLICIT_HPP
