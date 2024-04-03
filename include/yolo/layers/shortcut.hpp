#ifndef TADS_SHORTCUT_HPP
#define TADS_SHORTCUT_HPP

#include <map>

#include "layers/slice.hpp"
#include "layers/activation.hpp"

bool shortcutLayer(nvinfer1::ITensor *&output, int index, std::string_view activation, std::string_view input_vol,
									 std::string_view shortcut_vol, const std::map<std::string, std::string> &block, nvinfer1::ITensor *input,
									 nvinfer1::ITensor *shortcut_input, nvinfer1::INetworkDefinition *network, uint batch_size);
#endif // TADS_SHORTCUT_HPP
