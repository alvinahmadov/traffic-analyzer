#ifndef TADS_UTILS_HPP
#define TADS_UTILS_HPP

#include <string>
#include <vector>
#include <NvInfer.h>

#ifndef TADS_ERR_MSG_V
#define TADS_ERR_MSG_V(msg, ...) printf("** ERROR: <%s:%d>: " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifndef TADS_INFO_MSG_V
#define TADS_INFO_MSG_V(msg, ...) printf("** INFO: <%s:%d>: " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifndef TADS_DBG_MSG_V
#define TADS_DBG_MSG_V(msg, ...) printf("** DEBUG: <%s:%d>: " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifndef TADS_WARN_MSG_V
#define TADS_WARN_MSG_V(msg, ...) printf("** WARN: <%s:%d>: " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifndef TADS_CUDA_CHECK
#define TADS_CUDA_CHECK(status)                                       \
	{                                                                   \
		if(status != 0)                                                   \
		{                                                                 \
			TADS_ERR_MSG_V("CUDA failure: %s", cudaGetErrorString(status)); \
			abort();                                                        \
		}                                                                 \
	}
#endif

std::vector<float> load_weights(std::string_view weights_file_path, const std::string &modelName);

std::string dims_to_string(const nvinfer1::Dims &d);

int get_num_channels(nvinfer1::ITensor *tensor);

void print_layer_info(std::string_view index, std::string_view name, std::string_view input, std::string_view output,
											std::string_view weight_ptr);

bool file_exists(std::string_view file_name, bool verbose = true);

std::string trim1(std::string s);

float clamp(float val, float min_val, float max_val);

#endif // TADS_UTILS_HPP
