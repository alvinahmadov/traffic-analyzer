#ifndef TADS_ACTIVATION_HPP
#define TADS_ACTIVATION_HPP

#include <string>

#include <nvdsinfer.h>

/**
 *
 * \param output[out] output tensor
 * \param index[in] - layer instance_num
 * \param activation[in] - layer type
 * \param input[in] - input tensor
 * \param network[in] - network
 * \param name[in] - layer name
 * */
bool activationLayer(nvinfer1::ITensor *&output, int index, std::string_view activation, nvinfer1::ITensor *input,
										 nvinfer1::INetworkDefinition *network, const std::string &name = "");

#endif
