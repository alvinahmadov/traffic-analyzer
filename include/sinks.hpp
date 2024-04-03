#ifndef TADS_SINKS_HPP
#define TADS_SINKS_HPP

#include "common.hpp"

enum class SinkType
{
	FAKE = 1, ///< Fakesink
#ifndef IS_TEGRA
	RENDER_EGL = 2, ///< EGL based windowed nveglglessink for dGPU
#else
	RENDER_3D = 2, ///< EGL based windowed nv3dsink for Jetson
#endif
	ENCODE_FILE = 3,		 ///< Encode + File Save (encoder + muxer + filesink)
	RTSP = 4,						 ///< Encode + RTSP streaming; Note: sync=1 for this type is not applicable;
	RENDER_DRM = 5,			 ///< nvdrmvideosink (Jetson only)
	MSG_CONV_BROKER = 6, ///< Message converter + Message broker
};

enum class ContainerType
{
	MP4 = 1,
	MKV
};

enum class EncoderCodecType
{
	H264 = 1, ///< H.264 (hardware)
	H265,			///< H.265 (hardware)
	MPEG4
};

enum class EncoderEngineType
{
	NVENC = 0, ///< NVENC hardware engine
	CPU				 ///< CPU software encoder
};

enum class EncOutputIOMode
{
	MMAP = 2,
	DMABUF_IMPORT = 5,
};

struct SinkEncoderConfig : BaseConfig
{
	SinkType type{ SinkType::FAKE };
	/**
	 * Container to use for the file save.
	 * Only valid for @see ENCODE_FILE.
	 * */
	ContainerType container{ ContainerType::MP4 };
	/**
	 * The encoder to be used to save the file.
	 * */
	EncoderCodecType codec{ EncoderCodecType::H264 };
	/**
	 * Engine to use for encoder
	 *
	 * @example enc-type=0
	 * */
	EncoderEngineType enc_type{ EncoderEngineType::NVENC };
	EncOutputIOMode output_io_mode;
	/**
	 * Bitrate to use for encoding, in bits per second.
	 * Valid for @see ENCODE_FILE and @see RTSP.
	 * */
	int bitrate;
	/**
	 * Indicates how fast the stream is to be rendered.
	 * 0: As fast as possible
	 * 1: Synchronously
	 * */
	int sync;
	uint profile;
	std::string output_file;
	std::string output_file_path;
	uint gpu_id;
	/**
	 * Port for the RTSP streaming server;
	 * a valid unused port number.
	 * Valid for type=RTSP.
	 * */
	uint rtsp_port{ 8554 };
	/**
	 * Port used internally by the streaming implementation
	 * - a valid unused port number.
	 * Valid for type=RTSP.
	 * */
	uint udp_port{ 5000 };
	/**
	 * Encoding intra-frame occurrence frequency.
	 *
	 * @example iframeinterval=30
	 * */
	uint iframeinterval;
	uint copy_meta;
	uint64_t udp_buffer_size;
	int sw_preset;
};

struct SinkRenderConfig : BaseConfig
{
	SinkType type{ SinkType::FAKE };
	/**
	 * Width of the renderer in pixels.
	 *
	 * @example width=1920
	 * */
	int width{ 1280 };
	/**
	 * Height of the renderer in pixels.
	 *
	 * @example height=1080
	 * */
	int height{ 720 };
	/**
	 * Indicates how fast the stream is to be rendered.
	 * 0: As fast as possible
	 * 1: Synchronously
	 * */
	int sync{ 1 };
	/**
	 * Indicates whether the sink is to generate Quality-of-Service
	 * events, which can lead to the pipeline dropping frames when
	 * pipeline FPS cannot keep up with the stream frame rate.
	 *
	 * @example qos=0
	 * */
	bool qos{};
	uint gpu_id;
	NvBufMemoryType nvbuf_memory_type;
	uint offset_x;
	uint offset_y;
	uint color_range{ static_cast<uint>(-1) };
	uint conn_id;
	uint plane_id;
	bool set_mode{};
};

struct SinkMsgConvBrokerConfig : BaseConfig
{
	/**
	 * Enables or disables the message converter.
	 * */
	bool enable;
	// MsgConv settings
	/**
	 * comp-id Gst property of the gst-nvmsgconv element.
	 * This is the Id of the component that attaches the
	 * NvDsEventMsgMeta which must be processed by
	 * gst-nvmsgconv element.
	 * */
	uint conv_comp_id;
	/**
	 * Type of payload.
	 *
	 * 0 - PAYLOAD_DEEPSTREAM: Deepstream schema payload.
	 * 1 - PAYLOAD_DEEPSTREAM_MINIMAL: Deepstream schema payload minimal.
	 * 256, PAYLOAD_RESERVED: Reserved type.
	 * 257, PAYLOAD_CUSTOM: Custom schema payload.
	 * */
	uint conv_payload_type;
	/**
	 * Pathname of the configuration file for
	 * the Gst-nvmsgconv element.
	 * */
	std::string config_file_path;
	/**
	 * Absolute pathname of an optional custom payload
	 * generation library.
	 * This library implements the API defined by
	 * sources/libs/nvmsgconv/nvmsgconv.h.
	 * */
	std::string conv_msg2p_lib;
	/**
	 * Directory to dump payload.
	 * */
	std::string debug_payload_dir;
	/**
	 * Generate multiple message payloads.
	 * */
	bool multiple_payloads;
	/**
	 * Generate payloads using Gst buffer frame/object metadata.
	 * */
	bool conv_msg2p_new_api{};
	/**
	 * Frame interval at which payload is generated.
	 * */
	uint conv_frame_interval{ 30 };
	// Broker settings

	/**
	 * Path to the protocol adapter implementation
	 * used Gst-nvmsgbroker (type=6).
	 *
	 * @example
	 * msg-broker-proto-lib=/opt/nvidia/deepstream/deepstream/lib/libnvds_amqp_proto.so
	 * */
	std::string proto_lib;
	/**
	 * Connection string of the backend server (type=6).
	 *
	 * @example msg-broker-conn-str=foo.bar.com;80;dsapp
	 * */
	std::string conn_str;
	/**
	 * Name of the message topic (type=6).
	 *
	 * \example topic=test-ds4
	 * */
	std::string topic;
	/**
	 * Pathname of an optional configuration file
	 * for the Gst-nvmsgbroker element (type=6).
	 *
	 * @example msg-broker-config=/home/ubuntu/cfg_amqp.txt
	 * */
	std::string broker_config_file_path;
	/**
	 * comp-id Gst property of the nvmsgbroker element;
	 * ID (gie-unique-id) of the primary/secondary gie
	 * component from which metadata is to be processed.
	 * (type=6)
	 * */
	uint broker_comp_id;
	bool disable_msgconv;
	int sync;
	bool new_api{};
};

struct SinkSubBinConfig : BaseConfig
{
	bool enable;
	uint source_id;
	bool link_to_demux{};
	SinkType type;
	int sync;
	SinkEncoderConfig encoder_config;
	SinkRenderConfig render_config;
	SinkMsgConvBrokerConfig msg_conv_broker_config;
};

struct SinkSubBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *transform;
	GstElement *cap_filter;
	[[maybe_unused]] GstElement *enc_caps_filter;
	GstElement *encoder;
	GstElement *codecparse;
	GstElement *mux;
	GstElement *sink;
	GstElement *rtppay;
	[[maybe_unused]] gulong sink_buffer_probe;
};

struct SinkBin : BaseBin
{
	GstElement *bin;
	GstElement *queue;
	GstElement *tee;

	size_t num_bins;
	std::vector<SinkSubBin> sub_bins{ MAX_SINK_BINS };
};

/**
 * Initialize @ref NvDsSinkBin. It creates and adds sink and
 * other elements needed for processing to the bin.
 * It also sets properties mentioned in the configuration file under
 * group @ref CONFIG_GROUP_SINK
 *
 * @param[in] num_sub_bins number of sink elements.
 * @param[in] configs array of pointers of type @ref NvDsSinkSubBinConfig
 *            parsed from configuration file.
 * @param[in] sink pointer to @ref NvDsSinkBin to be filled.
 * @param[in] index id of source element.
 *
 * @return true if bin created successfully.
 */
bool create_sink_bin(uint num_sub_bins, std::vector<SinkSubBinConfig> &configs, SinkBin *sink, uint index);
bool create_demux_sink_bin(uint num_sub_bins, std::vector<SinkSubBinConfig> &configs, SinkBin *bin, uint index);
void destroy_sink_bin();

#endif // TADS_SINKS_HPP
