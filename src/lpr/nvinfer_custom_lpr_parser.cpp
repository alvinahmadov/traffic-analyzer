#include <glib.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstring>
#include <iostream>
#include <vector>
#include <fstream>

#include <nvdsinfer.h>

using namespace std;
using std::string;
using std::vector;

static const size_t SOFTMAX_SIZE = 16;
static const char *TADS_DICT_PATH_ENV = g_getenv("TADS_DICT_PATH");
static const char *TADS_LPR_MIN_CONF = g_getenv("TADS_LPR_MIN_CONF");
const size_t MINIMAL_CHAR_LEN{ 3 };

static const std::string DICT_PATH = TADS_DICT_PATH_ENV != nullptr ? TADS_DICT_PATH_ENV : "../data/configs/dict.txt";

const unordered_map<std::string, std::string> g_replace_chars{
	{ "Q", "O" },
	{ "J", "1" },
};

static std::string &transform_attr_string(std::string &label)
{
	std::vector<bool> mask;

	std::transform(label.begin(), label.end(), label.begin(), [](char &c) { return std::isalnum(c) ? c : char(); });

	for(char c : label)
	{
		mask.push_back(std::isdigit(c));
	}

	if(!mask.empty())
	{
		bool matches_mask = std::all_of(mask.begin() + 1, mask.begin() + 4, [](bool item) { return item; });

		if(matches_mask)
		{
			if(label.front() == '0' || label.front() == '6')
			{
				label.front() = 'O';
			}
			else if(label.front() == '8')
			{
				label.front() = 'B';
			}
			else if(label.front() == '1')
			{
				label.front() = 'T';
			}

			if(mask.size() > 4 and mask.at(4))
			{
				char &item = label.at(4);
				if(item == '0')
				{
					item = 'O';
				}
				else if(item == '8')
				{
					item = 'B';
				}
			}

			if(mask.size() > 5 && mask.at(5))
			{
				char &item = label.at(5);

				if(item == '0')
				{
					item = 'O';
				}
				else if(item == '8')
				{
					item = 'B';
				}
				else if(item == '7')
				{
					item = 'T';
				}
			}
		}
	}

	return label;
}

std::string localize(const std::string &text)
{
	std::string localized_str(text.length(), '\0');
	std::transform(text.cbegin(), text.cend(), localized_str.begin(), [](char c) { return std::toupper(c); });

	for(const auto &[k, v] : g_replace_chars)
	{
		if(localized_str == k)
			localized_str = v;
	}
	return localized_str;
}

static bool g_dict_ready{};
static std::vector<string> g_dict_table;

extern "C" [[maybe_unused]]
bool NvDsInferParseCustomNVPlate(std::vector<NvDsInferLayerInfo> const &output_layers_info,
																 NvDsInferNetworkInfo const &network_info, float threshold,
																 std::vector<NvDsInferAttribute> &attributes, std::string &attribute_label);

extern "C" [[maybe_unused]]
bool NvDsInferParseCustomNVPlate(std::vector<NvDsInferLayerInfo> const &output_layers_info,
																 NvDsInferNetworkInfo const &network_info, float,
																 std::vector<NvDsInferAttribute> &attributes, std::string &attribute_label)
{
	int *output_str_buffer{};
	float *output_conf_buffer{};
	float min_threshold = TADS_LPR_MIN_CONF != nullptr ? std::strtof(TADS_LPR_MIN_CONF, nullptr) : 0.45;
	NvDsInferAttribute lpr_attr;
	float attribute_confidence{ 1 };

	int seq_len;

	// Get list
	vector<int> label_indexes;
	int prev = 100;

	// For confidence
	std::vector<double> bank_softmax_max(SOFTMAX_SIZE, 0.0);
	uint valid_bank_count{ 0 };
	bool do_softmax{};
	ifstream dict_file;

	setlocale(LC_CTYPE, "");

	if(!g_dict_ready)
	{
		dict_file.open(DICT_PATH);
		if(!dict_file.is_open())
		{
			cerr << "open dictionary file failed." << endl;
			return false;
		}
		while(!dict_file.eof())
		{
			string str_line_ansi;
			if(getline(dict_file, str_line_ansi))
			{
				g_dict_table.push_back(str_line_ansi);
			}
		}
		g_dict_ready = true;
		dict_file.close();
	}

	const int labels_size = g_dict_table.size();
	int layer_size = output_layers_info.size();

	seq_len = network_info.width / 4;

	for(int layer_index = 0; layer_index < layer_size; layer_index++)
	{
		if(!output_layers_info[layer_index].isInput)
		{
			if(output_layers_info[layer_index].dataType == NvDsInferDataType::FLOAT)
			{
				if(output_conf_buffer == nullptr)
					output_conf_buffer = static_cast<float *>(output_layers_info[layer_index].buffer);
			}
			else if(output_layers_info[layer_index].dataType == NvDsInferDataType::INT32)
			{
				if(output_str_buffer == nullptr)
					output_str_buffer = static_cast<int *>(output_layers_info[layer_index].buffer);
			}
		}
	}

	for(int seq_id = 0; seq_id < seq_len; seq_id++)
	{

		if(output_str_buffer == nullptr)
			continue;

		int curr_data = output_str_buffer[seq_id];
		if(curr_data < 0 || curr_data > labels_size)
		{
			continue;
		}
		if(seq_id == 0)
		{
			prev = curr_data;
			label_indexes.push_back(curr_data);
			if(curr_data != labels_size)
			{
				do_softmax = true;
			}
		}
		else
		{
			if(curr_data != prev)
			{
				label_indexes.push_back(curr_data);
				if(curr_data != labels_size)
				{
					do_softmax = true;
				}
			}
			prev = curr_data;
		}

		// Do softmax
		if(do_softmax)
		{
			if(output_conf_buffer != nullptr)
			{
				bank_softmax_max.at(valid_bank_count++) = output_conf_buffer[seq_id];
			}
			do_softmax = false;
		}
	}

	for(int label_index : label_indexes)
	{
		if(label_index != labels_size)
		{
			attribute_label.append(localize(g_dict_table[label_index]));
		}
	}

	// Ignore the short string, it may be wrong plate string
	if(valid_bank_count > MINIMAL_CHAR_LEN && !attribute_label.empty())
	{
		transform_attr_string(attribute_label);

		for(uint i{}; i < valid_bank_count; i++)
		{
			float conf = bank_softmax_max.at(i);
			if(conf < min_threshold)
			{
				attribute_confidence = 0.0;
				break;
			}
			attribute_confidence *= conf;
		}

		if(attribute_confidence > 0.0)
		{
			char *label = strdup(attribute_label.c_str());
			lpr_attr = NvDsInferAttribute{ 0, 1, attribute_confidence, label };
			attributes.emplace_back(lpr_attr);
		}
	}

	return true;
}
