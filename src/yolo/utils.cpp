#include <iostream>
#include <iomanip>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include "utils.hpp"

std::vector<float> load_weights(std::string_view weights_file_path, const std::string &modelName)
{
	assert(file_exists(weights_file_path));
	TADS_INFO_MSG_V("Loading pre-trained weights");

	std::vector<float> weights;

	if(weights_file_path.find(".weights") != std::string::npos)
	{
		std::ifstream file(weights_file_path.data(), std::ios_base::binary);
		assert(file.good());
		std::string line;

		if(modelName.find("yolov2") != std::string::npos && modelName.find("yolov2-tiny") == std::string::npos)
		{
			// Remove 4 int32 bytes of data from the stream belonging to the header
			file.ignore(4 * 4);
		}
		else
		{
			// Remove 5 int32 bytes of data from the stream belonging to the header
			file.ignore(4 * 5);
		}

		char floatWeight[4];
		while(!file.eof())
		{
			file.read(floatWeight, 4);
			assert(file.gcount() == 4);
			weights.push_back(*reinterpret_cast<float *>(floatWeight));
			if(file.peek() == std::istream::traits_type::eof())
			{
				break;
			}
		}
	}
	else
	{
		TADS_ERR_MSG_V("File %s is not supported", weights_file_path.data());
	}

	TADS_INFO_MSG_V("Loading weights of %s complete", modelName.data());
	TADS_INFO_MSG_V("Total weights read: %ld", weights.size());
	return weights;
}

std::string dims_to_string(const nvinfer1::Dims &d)
{
	assert(d.nbDims >= 1);

	std::stringstream s;
	s << "[";
	for(int i = 1; i < d.nbDims - 1; ++i)
	{
		s << d.d[i] << ", ";
	}
	s << d.d[d.nbDims - 1] << "]";

	return s.str();
}

int get_num_channels(nvinfer1::ITensor *tensor)
{
	nvinfer1::Dims d = tensor->getDimensions();
	assert(d.nbDims == 4);
	return d.d[1];
}

void print_layer_info(std::string_view index, std::string_view name, std::string_view input, std::string_view output,
											std::string_view weight_ptr)
{
	std::cout << std::setw(7) << std::left << index << std::setw(40) << std::left << name;
	std::cout << std::setw(19) << std::left << input << std::setw(19) << std::left << output;
	std::cout << weight_ptr << std::endl;
}

bool file_exists(std::string_view file_name, bool verbose)
{
	if(!std::filesystem::exists(std::filesystem::path(file_name)))
	{
		if(verbose)
		{
			TADS_WARN_MSG_V("File does not exist: %s", file_name.data());
		}
		return false;
	}
	return true;
}

static void leftTrim(std::string &s)
{
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
}

static void rightTrim(std::string &s)
{
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(), s.end());
}

std::string trim1(std::string s)
{
	leftTrim(s);
	rightTrim(s);
	return s;
}

[[maybe_unused]]
float clamp(float val, float min_val, float max_val)
{
	assert(min_val <= max_val);
	return std::min(max_val, std::max(min_val, val));
}