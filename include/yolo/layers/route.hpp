#ifndef TADS_ROUTE_HPP
#define TADS_ROUTE_HPP

#include <map>
#include <vector>

#include "layers/slice.hpp"

bool routeLayer(nvinfer1::ITensor *&output, int index, std::string &layers,
								const std::map<std::string, std::string> &block, std::vector<nvinfer1::ITensor *> tensor_outputs,
								nvinfer1::INetworkDefinition *network, uint batch_size);

#endif // TADS_ROUTE_HPP
