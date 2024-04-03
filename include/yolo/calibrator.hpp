#ifndef TADS_CALIBRATOR_HPP
#define TADS_CALIBRATOR_HPP

#include <vector>

#include <cuda_runtime_api.h>
#include <NvInfer.h>
#include <opencv2/opencv.hpp>

class Int8EntropyCalibrator2 final : public nvinfer1::IInt8EntropyCalibrator2
{
public:
	Int8EntropyCalibrator2(const int &batch_size, const int &channels, const int &height, const int &width,
												 const float &scale_factor, const float *offsets, const std::string &img_path,
												 const std::string &calib_table_path);

	~Int8EntropyCalibrator2() final;

	[[nodiscard]]
	int getBatchSize() const noexcept override;

	bool getBatch(void *bindings[], const char *names[], int nb_bindings) noexcept override;

	const void *readCalibrationCache(std::size_t &length) noexcept override;

	void writeCalibrationCache(const void *cache, size_t length) noexcept override;

private:
	int m_batch_size;
	int m_input_c;
	int m_input_h;
	int m_input_w;
	[[maybe_unused]] int m_letter_box;
	float m_scale_factor;
	const float *m_offsets;
	std::string m_calib_table_path;
	size_t m_image_index;
	size_t m_input_count;
	std::vector<std::string> m_img_paths;
	float *m_batch_data{};
	void *m_device_input{ nullptr };
	bool m_read_cache;
	std::vector<char> m_calibration_cache;
};

std::vector<float>
prepareImage(cv::Mat &img, int input_c, int input_h, int input_w, float scale_factor, const float *offsets);

#endif // TADS_CALIBRATOR_HPP
