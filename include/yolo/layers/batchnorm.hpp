#ifndef TADS_BATCHNORM_HPP
#define TADS_BATCHNORM_HPP

#include <map>
#include <vector>

#include <NvInfer.h>

#include "activation.hpp"

/**
 *
 * \param output[out] output tensor
 * \param index[in] - layer instance_num
 * \param block[in]
 * \param weights[in]
 * \param trt_weights[in] tensor weights
 * \param weight_ptr[in]
 * \param input[in] - input tensor
 * \param network[in] - network
 * */
bool batchnormLayer(nvinfer1::ITensor *&output, int index, const std::map<std::string, std::string> &block,
										std::vector<float> &weights, std::vector<nvinfer1::Weights> &trt_weights, int &weight_ptr,
										nvinfer1::ITensor *input, nvinfer1::INetworkDefinition *network);

#endif