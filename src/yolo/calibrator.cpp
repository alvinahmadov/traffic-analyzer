#include <fstream>
#include <iterator>

#include "utils.hpp"
#include "calibrator.hpp"

Int8EntropyCalibrator2::Int8EntropyCalibrator2(const int &batch_size, const int &channels, const int &height,
																							 const int &width, const float &scale_factor, const float *offsets,
																							 const std::string &img_path,
																							 const std::string &calib_table_path): // NOLINT(*-pass-by-value)
	m_batch_size(batch_size),
	m_input_c(channels),
	m_input_h(height),
	m_input_w(width),
	m_scale_factor(scale_factor),
	m_offsets(offsets),
	m_calib_table_path(calib_table_path),
	m_image_index()
{
	m_input_count = batch_size * channels * height * width;
	std::fstream f(img_path);
	if(f.is_open())
	{
		std::string temp;
		while(std::getline(f, temp))
		{
			m_img_paths.push_back(temp);
		}
	}
	m_batch_data = new float[m_input_count];
	TADS_CUDA_CHECK(cudaMalloc(&m_device_input, m_input_count * sizeof(float)))
}

Int8EntropyCalibrator2::~Int8EntropyCalibrator2()
{
	TADS_CUDA_CHECK(cudaFree(m_device_input))
	delete[] m_batch_data;
}

int Int8EntropyCalibrator2::getBatchSize() const noexcept
{
	return m_batch_size;
}

bool Int8EntropyCalibrator2::getBatch(void **bindings, const char **names, int nb_bindings) noexcept
{
	if(m_image_index + m_batch_size > uint(m_img_paths.size()))
	{
		return false;
	}

	float *ptr = m_batch_data;
	for(size_t i = m_image_index; i < m_image_index + m_batch_size; ++i)
	{
		cv::Mat img = cv::imread(m_img_paths[i]);
		if(img.empty())
		{
			TADS_ERR_MSG_V("Failed to read image for calibration");
			return false;
		}

		std::vector<float> input_data = prepareImage(img, m_input_c, m_input_h, m_input_w, m_scale_factor, m_offsets);

		size_t len = input_data.size();
		memcpy(ptr, input_data.data(), len * sizeof(float));
		ptr += input_data.size();

		TADS_DBG_MSG_V("Load image: %s", m_img_paths.at(i).c_str());
		TADS_DBG_MSG_V("Progress: %f%%", (i + 1) * 100. / m_img_paths.size());
	}

	m_image_index += m_batch_size;

	TADS_CUDA_CHECK(cudaMemcpy(m_device_input, m_batch_data, m_input_count * sizeof(float), cudaMemcpyHostToDevice))
	bindings[0] = m_device_input;

	return true;
}

const void *Int8EntropyCalibrator2::readCalibrationCache(std::size_t &length) noexcept
{
	m_calibration_cache.clear();
	std::ifstream input(m_calib_table_path, std::ios::binary);
	input >> std::noskipws;
	if(m_read_cache && input.good())
	{
		std::copy(std::istream_iterator<char>(input), std::istream_iterator<char>(),
							std::back_inserter(m_calibration_cache));
	}
	length = m_calibration_cache.size();
	return length ? m_calibration_cache.data() : nullptr;
}

void Int8EntropyCalibrator2::writeCalibrationCache(const void *cache, std::size_t length) noexcept
{
	std::ofstream output(m_calib_table_path, std::ios::binary);
	output.write(reinterpret_cast<const char *>(cache), length);
}

std::vector<float>
prepareImage(cv::Mat &img, int input_c, int input_h, int input_w, float scale_factor, const float *offsets)
{
	cv::Mat out;

	cv::cvtColor(img, out, cv::COLOR_BGR2RGB);

	int image_w = img.cols;
	int image_h = img.rows;

	if(image_w != input_w || image_h != input_h)
	{
		float resize_factor = std::max(input_w / (float)image_w, input_h / (float)img.rows);
		cv::resize(out, out, cv::Size(0, 0), resize_factor, resize_factor, cv::INTER_CUBIC);
		cv::Rect crop(cv::Point(0.5 * (out.cols - input_w), 0.5 * (out.rows - input_h)), cv::Size(input_w, input_h));
		out = out(crop);
	}

	out.convertTo(out, CV_32F, scale_factor);
	cv::subtract(out, cv::Scalar(offsets[2] / 255, offsets[1] / 255, offsets[0] / 255), out, cv::noArray(), -1);

	std::vector<cv::Mat> input_channels(input_c);
	cv::split(out, input_channels);
	std::vector<float> result(input_h * input_w * input_c);
	auto data = result.data();
	int channel_length = input_h * input_w;
	for(int i = 0; i < input_c; ++i)
	{
		memcpy(data, input_channels[i].data, channel_length * sizeof(float));
		data += channel_length;
	}

	return result;
}