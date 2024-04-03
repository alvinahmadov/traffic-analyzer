#ifndef TADS_CONFIG_HPP
#define TADS_CONFIG_HPP

#include <string>
#include <string_view>

#include <nvds_version.h>

#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 0
#define APP_VERSION_MICRO 0

#undef TADS_APP_DEBUG
#undef TADS_PRIMARY_GIE_DEBUG
#undef TADS_SECONDARY_GIE_DEBUG
#undef TADS_TRACKER_DEBUG
#undef TADS_ANALYTICS_DEBUG
#undef TADS_CONFIG_PARSER_DEBUG

#ifdef __aarch64__
#define IS_TEGRA
#endif

constexpr size_t MAX_SOURCE_BINS{ 1 };
constexpr size_t MAX_SINK_BINS{ 2 };
constexpr size_t MAX_SECONDARY_GIE_BINS{ 5 };
constexpr size_t MAX_SECONDARY_PREPROCESS_BINS{ 5 };
constexpr size_t MAX_MESSAGE_CONSUMERS{ 5 };

const std::string MEMORY_FEATURES{ "memory:NVMM" };

#ifdef IS_TEGRA
constexpr std::string_view TADS_ELEM_SRC_CAMERA_CSI{ "nvarguscamerasrc" };
#else
constexpr std::string_view TADS_ELEM_SRC_CAMERA_CSI{ "videotestsrc" };
#endif

constexpr std::string_view TADS_ELEM_SRC_CAMERA_V4L2{ "v4l2src" };
constexpr std::string_view TADS_ELEM_SRC_URI{ "uridecodebin" };

constexpr std::string_view TADS_ELEM_VIDEO_CONV{ "multifilesrc" };

constexpr std::string_view TADS_ELEM_QUEUE{ "queue" };
constexpr std::string_view TADS_ELEM_DECODEBIN{ "decodebin" };
constexpr std::string_view TADS_ELEM_CAPS_FILTER{ "capsfilter" };
constexpr std::string_view TADS_ELEM_TEE{ "tee" };
constexpr std::string_view TADS_ELEM_IDENTITY{ "identity" };

constexpr std::string_view TADS_ELEM_PREPROCESS{ "nvdspreprocess" };
constexpr std::string_view TADS_ELEM_SECONDARY_PREPROCESS{ "nvdspreprocess" };
constexpr std::string_view TADS_ELEM_NVINFER{ "nvinfer" };
constexpr std::string_view TADS_ELEM_NVINFER_SERVER{ "nvinferserver" };
constexpr std::string_view TADS_ELEM_TRACKER{ "nvtracker" };

constexpr std::string_view TADS_ELEM_NVVIDEO_CONV{ "nvvideoconvert" };
constexpr std::string_view TADS_ELEM_STREAMMUX{ "nvstreammux" };
constexpr std::string_view TADS_ELEM_STREAM_DEMUX{ "nvstreamdemux" };
constexpr std::string_view TADS_ELEM_TILER{ "nvmultistreamtiler" };
constexpr std::string_view TADS_ELEM_OSD{ "nvdsosd" };
constexpr std::string_view TADS_ELEM_DSANALYTICS_ELEMENT{ "nvdsanalytics" };

constexpr std::string_view TADS_ELEM_MSG_CONV{ "nvmsgconv" };
constexpr std::string_view TADS_ELEM_MSG_BROKER{ "nvmsgbroker" };

constexpr std::string_view TADS_ELEM_SINK_FAKESINK{ "fakesink" };
constexpr std::string_view TADS_ELEM_SINK_FILE{ "filesink" };
#ifndef IS_TEGRA
constexpr std::string_view TADS_ELEM_SINK_EGL{ "nveglglessink" };
#else
constexpr std::string_view TADS_ELEM_SINK_3D{ "nv3dsink" };
#endif
constexpr std::string_view TADS_ELEM_SINK_UDP{ "udpsink" };
constexpr std::string_view TADS_ELEM_SINK_DRM{ "nvdrmvideosink" };
constexpr std::string_view TADS_ELEM_EGLTRANSFORM{ "nvegltransform" };

constexpr std::string_view TADS_ELEM_RTSPSRC{ "rtspsrc" };

constexpr std::string_view TADS_ELEM_MUX_MP4{ "qtmux" };
constexpr std::string_view TADS_ELEM_MKV{ "matroskamux" };

constexpr std::string_view TADS_ELEM_ENC_H264_HW{ "nvv4l2h264enc" };
constexpr std::string_view TADS_ELEM_ENC_H265_HW{ "nvv4l2h265enc" };
constexpr std::string_view TADS_ELEM_ENC_MPEG4{ "avenc_mpeg4" };

constexpr std::string_view TADS_ELEM_ENC_H264_SW{ "x264enc" };
constexpr std::string_view TADS_ELEM_ENC_H265_SW{ "x265enc" };

constexpr std::string_view TADS_ELEM_NVMULTIURISRCBIN{ "nvmultiurisrcbin" };

constexpr std::string_view CONFIG_GROUP_SOURCE_ALL{ "source-attr-all" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST{ "source-list" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_NUM_SOURCE_BINS{ "num-source-bins" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_URI_LIST{ "list" };
/** this vector is one to one mapped with the uri-list/list */
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_SENSOR_ID_LIST{ "sensor-id-list" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_SENSOR_NAME_LIST{ "sensor-name-list" };

/** additional configs to support nvmultiurisrcbin usage */
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_USE_NVMULTIURISRCBIN{ "use-nvmultiurisrcbin" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_STREAM_NAME_DISPLAY{ "stream-name-display" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_MAX_BATCH_SIZE{ "max-batch-size" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_HTTP_IP{ "http-ip" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIST_HTTP_PORT{ "http-port" };

constexpr std::string_view CONFIG_KEY_ID{ "id" };
constexpr std::string_view CONFIG_KEY_GPU_ID{ "gpu-id" };
constexpr std::string_view CONFIG_KEY_CUDA_MEMORY_TYPE{ "nvbuf-memory-type" };
constexpr std::string_view CONFIG_KEY_ENABLE{ "enable" };

// SOURCE

constexpr std::string_view CONFIG_GROUP_SOURCE{ "source" };
constexpr std::string_view CONFIG_GROUP_SOURCE_TYPE{ "type" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_WIDTH{ "camera-width" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_HEIGHT{ "camera-height" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_FPS_N{ "camera-g_fps-n" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_FPS_D{ "camera-g_fps-d" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_CSI_SID{ "camera-csi-sensor-id" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_V4L2_DEVNODE{ "camera-v4l2-dev-node" };
constexpr std::string_view CONFIG_GROUP_SOURCE_URI{ "uri" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LATENCY{ "latency" };
constexpr std::string_view CONFIG_GROUP_SOURCE_NUM_SOURCES{ "num-sources" };
constexpr std::string_view CONFIG_GROUP_SOURCE_INTRA_DECODE{ "intra-decode-enable" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LOW_LATENCY_DECODE{ "low-latency-decode" };
constexpr std::string_view CONFIG_GROUP_SOURCE_NUM_DECODE_SURFACES{ "num-decode-surfaces" };
constexpr std::string_view CONFIG_GROUP_SOURCE_NUM_EXTRA_SURFACES{ "num-extra-surfaces" };
constexpr std::string_view CONFIG_GROUP_SOURCE_DROP_FRAME_INTERVAL{ "drop-frame-interval" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CAMERA_ID{ "camera-id" };
constexpr std::string_view CONFIG_GROUP_SOURCE_ID{ "source-id" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SELECT_RTP_PROTOCOL{ "select-rtp-protocol" };
constexpr std::string_view CONFIG_GROUP_SOURCE_RTSP_RECONNECT_INTERVAL_SEC{ "rtsp-reconnect-interval-sec" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_ENABLE{ "smart-record" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_DIRPATH{ "smart-rec-dir-path" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_FILE_PREFIX{ "smart-rec-file-prefix" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE_LEGACY{ "smart-rec-video-cache" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_CACHE_SIZE{ "smart-rec-cache" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_CONTAINER{ "smart-rec-container" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_START_TIME{ "smart-rec-start-time" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_DEFAULT_DURATION{ "smart-rec-default-duration" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_DURATION{ "smart-rec-duration" };
constexpr std::string_view CONFIG_GROUP_SOURCE_SMART_RECORD_INTERVAL{ "smart-rec-interval" };
constexpr std::string_view CONFIG_GROUP_SOURCE_UDP_BUFFER_SIZE{ "udp-buffer-size" };
constexpr std::string_view CONFIG_GROUP_SOURCE_VIDEO_FORMAT{ "video-format" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CSV_PATH{ "csv-file-path" };
constexpr std::string_view CONFIG_GROUP_SOURCE_LIVE_SOURCE{ "live-source" };
constexpr std::string_view CONFIG_GROUP_SOURCE_CUDADEC_MEMTYPE{ "cudadec-memtype" };
constexpr std::string_view CONFIG_GROUP_SOURCE_FILE_LOOP{ "file-loop" };
constexpr std::string_view CONFIG_GROUP_SOURCE_DEC_SKIP_FRAMES{ "dec-skip-frames" };
constexpr std::string_view CONFIG_GROUP_SOURCE_RTSP_RECONNECT_ATTEMPTS{ "rtsp-reconnect-attempts" };

// STREAMMUX

constexpr std::string_view CONFIG_GROUP_STREAMMUX{ "streammux" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_WIDTH{ "width" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_HEIGHT{ "height" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_BUFFER_POOL_SIZE{ "buffer-pool-size" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_BATCHED_PUSH_TIMEOUT{ "batched-push-timeout" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_LIVE_SOURCE{ "live-source" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_BATCH_SIZE{ "batch-size" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_ENABLE_PADDING{ "enable-padding" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_COMPUTE_HW{ "compute-hw" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_ATTACH_SYS_TS{ "attach-sys-ts" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_ATTACH_SYS_TS_AS_NTP{ "attach-sys-ts-as-ntp" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_STREAM_RESET{ "frame-num-reset-on-stream-reset" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_FRAME_NUM_RESET_ON_EOS{ "frame-num-reset-on-eos" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_FRAME_DURATION{ "frame-duration" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_CONFIG_FILE_PATH{ "config-file" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_SYNC_INPUTS{ "sync-inputs" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_MAX_LATENCY{ "max-latency" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_ASYNC_PROCESS{ "async-process" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_DROP_PIPELINE_EOS{ "drop-pipeline-eos" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_NUM_SURFACES_PER_FRAME{ "num-surfaces-per-frame" };
constexpr std::string_view CONFIG_GROUP_STREAMMUX_INTERP_METHOD{ "interpolation-method" };

// OSD

constexpr std::string_view CONFIG_GROUP_OSD{ "osd" };
constexpr std::string_view CONFIG_GROUP_OSD_MODE{ "process-mode" };
constexpr std::string_view CONFIG_GROUP_OSD_BORDER_WIDTH{ "border-width" };
constexpr std::string_view CONFIG_GROUP_OSD_BORDER_COLOR{ "border-color" };
constexpr std::string_view CONFIG_GROUP_OSD_TEXT_SIZE{ "text-size" };
constexpr std::string_view CONFIG_GROUP_OSD_TEXT_COLOR{ "text-color" };
constexpr std::string_view CONFIG_GROUP_OSD_TEXT_BG_COLOR{ "text-bg-color" };
constexpr std::string_view CONFIG_GROUP_OSD_FONT{ "font" };
constexpr std::string_view CONFIG_GROUP_OSD_HW_BLEND_COLOR_ATTR{ "hw-blend-color-attr" };
constexpr std::string_view CONFIG_GROUP_OSD_CLOCK_ENABLE{ "show-clock" };
constexpr std::string_view CONFIG_GROUP_OSD_CLOCK_X_OFFSET{ "clock-x-offset" };
constexpr std::string_view CONFIG_GROUP_OSD_CLOCK_Y_OFFSET{ "clock-y-offset" };
constexpr std::string_view CONFIG_GROUP_OSD_CLOCK_TEXT_SIZE{ "clock-text-size" };
constexpr std::string_view CONFIG_GROUP_OSD_CLOCK_COLOR{ "clock-color" };
constexpr std::string_view CONFIG_GROUP_OSD_SHOW_TEXT{ "display-text" };
constexpr std::string_view CONFIG_GROUP_OSD_SHOW_BBOX{ "display-bbox" };
constexpr std::string_view CONFIG_GROUP_OSD_SHOW_MASK{ "display-mask" };

// PREPROCESS

constexpr std::string_view CONFIG_GROUP_PREPROCESS{ "pre-process" };
constexpr std::string_view CONFIG_GROUP_SECONDARY_PREPROCESS{ "secondary-pre-process" };
constexpr std::string_view CONFIG_GROUP_PREPROCESS_CONFIG_FILE{ "config-file" };

// GIE

constexpr std::string_view CONFIG_GROUP_PRIMARY_GIE{ "primary-gie" };
constexpr std::string_view CONFIG_GROUP_SECONDARY_GIE{ "secondary-gie" };
constexpr std::string_view CONFIG_GROUP_GIE_INPUT_TENSOR_META{ "input-tensor-meta" };
constexpr std::string_view CONFIG_GROUP_GIE_BATCH_SIZE{ "batch-size" };
constexpr std::string_view CONFIG_GROUP_GIE_MODEL_ENGINE{ "model-engine-file" };
constexpr std::string_view CONFIG_GROUP_GIE_CONFIG_FILE{ "config-file" };
constexpr std::string_view CONFIG_GROUP_GIE_LABEL_FILE{ "labelfile-path" };
constexpr std::string_view CONFIG_GROUP_GIE_PLUGIN_TYPE{ "plugin-type" };
constexpr std::string_view CONFIG_GROUP_GIE_UNIQUE_ID{ "gie-unique-id" };
constexpr std::string_view CONFIG_GROUP_GIE_ID_FOR_OPERATION{ "operate-on-gie-id" };
constexpr std::string_view CONFIG_GROUP_GIE_BBOX_BORDER_COLOR{ "bbox-border-color" };
constexpr std::string_view CONFIG_GROUP_GIE_BBOX_BG_COLOR{ "bbox-bg-color" };
constexpr std::string_view CONFIG_GROUP_GIE_CLASS_IDS_FOR_OPERATION{ "operate-on-class-ids" };
constexpr std::string_view CONFIG_GROUP_GIE_CLASS_IDS_FOR_FILTER{ "filter-out-class-ids" };
constexpr std::string_view CONFIG_GROUP_GIE_INTERVAL{ "interval" };
constexpr std::string_view CONFIG_GROUP_GIE_RAW_OUTPUT_DIR{ "infer-raw-output-dir" };
constexpr std::string_view CONFIG_GROUP_GIE_FRAME_SIZE{ "audio-framesize" };
constexpr std::string_view CONFIG_GROUP_GIE_HOP_SIZE{ "audio-hopsize" };

// TRACKER

constexpr std::string_view CONFIG_GROUP_TRACKER{ "tracker" };
constexpr std::string_view CONFIG_GROUP_TRACKER_WIDTH{ "tracker-width" };
constexpr std::string_view CONFIG_GROUP_TRACKER_HEIGHT{ "tracker-height" };
constexpr std::string_view CONFIG_GROUP_TRACKER_ALGORITHM{ "tracker-algorithm" };
constexpr std::string_view CONFIG_GROUP_TRACKER_IOU_THRESHOLD{ "iou-threshold" };
constexpr std::string_view CONFIG_GROUP_TRACKER_SURFACE_TYPE{ "tracker-surface-type" };
constexpr std::string_view CONFIG_GROUP_TRACKER_LL_CONFIG_FILE{ "ll-config-file" };
constexpr std::string_view CONFIG_GROUP_TRACKER_LL_LIB_FILE{ "ll-lib-file" };
constexpr std::string_view CONFIG_GROUP_TRACKER_TRACKING_SURFACE_TYPE{ "tracking-surface-type" };
constexpr std::string_view CONFIG_GROUP_TRACKER_DISPLAY_TRACKING_ID{ "display-tracking-id" };
constexpr std::string_view CONFIG_GROUP_TRACKER_TRACKING_ID_RESET_MODE{ "tracking-id-reset-mode" };
constexpr std::string_view CONFIG_GROUP_TRACKER_INPUT_TENSOR_META{ "input-tensor-meta" };
constexpr std::string_view CONFIG_GROUP_TRACKER_TENSOR_META_GIE_ID{ "tensor-meta-gie-id" };
constexpr std::string_view CONFIG_GROUP_TRACKER_COMPUTE_HW{ "compute-hw" };
constexpr std::string_view CONFIG_GROUP_TRACKER_USER_META_POOL_SIZE{ "user-meta-pool-size" };
constexpr std::string_view CONFIG_GROUP_TRACKER_SUB_BATCHES{ "sub-batches" };

// SINK

constexpr std::string_view CONFIG_GROUP_SINK{ "sink" };
constexpr std::string_view CONFIG_GROUP_SINK_TYPE{ "type" };
constexpr std::string_view CONFIG_GROUP_SINK_WIDTH{ "width" };
constexpr std::string_view CONFIG_GROUP_SINK_HEIGHT{ "height" };
constexpr std::string_view CONFIG_GROUP_SINK_SYNC{ "sync" };
constexpr std::string_view CONFIG_GROUP_SINK_QOS{ "qos" };
constexpr std::string_view CONFIG_GROUP_SINK_CONTAINER{ "container" };
constexpr std::string_view CONFIG_GROUP_SINK_CODEC{ "codec" };
constexpr std::string_view CONFIG_GROUP_SINK_ENC_TYPE{ "enc-type" };
constexpr std::string_view CONFIG_GROUP_SINK_BITRATE{ "bitrate" };
constexpr std::string_view CONFIG_GROUP_SINK_PROFILE{ "profile" };
constexpr std::string_view CONFIG_GROUP_SINK_IFRAMEINTERVAL{ "iframeinterval" };
constexpr std::string_view CONFIG_GROUP_SINK_COPY_META{ "copy-meta" };
constexpr std::string_view CONFIG_GROUP_SINK_OUTPUT_IO_MODE{ "output-io-mode" };
constexpr std::string_view CONFIG_GROUP_SINK_SW_PRESET{ "sw-preset" };
constexpr std::string_view CONFIG_GROUP_SINK_OUTPUT_FILE{ "output-file" };
constexpr std::string_view CONFIG_GROUP_SINK_OUTPUT_FILE_PATH{ "output-file-path" };
constexpr std::string_view CONFIG_GROUP_SINK_SOURCE_ID{ "source-id" };
constexpr std::string_view CONFIG_GROUP_SINK_RTSP_PORT{ "rtsp-port" };
constexpr std::string_view CONFIG_GROUP_SINK_UDP_PORT{ "udp-port" };
constexpr std::string_view CONFIG_GROUP_SINK_UDP_BUFFER_SIZE{ "udp-buffer-size" };
constexpr std::string_view CONFIG_GROUP_SINK_COLOR_RANGE{ "color-range" };
constexpr std::string_view CONFIG_GROUP_SINK_CONN_ID{ "conn-id" };
constexpr std::string_view CONFIG_GROUP_SINK_PLANE_ID{ "plane-id" };
constexpr std::string_view CONFIG_GROUP_SINK_SET_MODE{ "set-mode" };
constexpr std::string_view CONFIG_GROUP_SINK_ONLY_FOR_DEMUX{ "link-to-demux" };

constexpr std::string_view CONFIG_GROUP_MSG_CONVERTER{ "message-converter" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_CONFIG{ "msg-conv-config" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_PAYLOAD_TYPE{ "msg-conv-payload-type" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_MSG2P_LIB{ "msg-conv-msg2p-lib" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_COMP_ID{ "msg-conv-comp-id" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_DEBUG_PAYLOAD_DIR{ "debug-payload-dir" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_MULTIPLE_PAYLOADS{ "multiple-payloads" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_MSG2P_NEW_API{ "msg-conv-msg2p-newapi" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_CONV_FRAME_INTERVAL{ "msg-conv-frame-interval" };

constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_PROTO_LIB{ "msg-broker-proto-lib" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_CONN_STR{ "msg-broker-conn-str" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_TOPIC{ "topic" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_CONFIG_FILE{ "msg-broker-config" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_COMP_ID{ "msg-broker-comp-id" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_DISABLE_MSG_CONVERTER{ "disable-msgconv" };
constexpr std::string_view CONFIG_GROUP_SINK_MSG_BROKER_NEW_API{ "new-api" };

// MSG_CONSUMER

constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER{ "message-consumer" };
constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER_CONFIG{ "config-file" };
constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER_PROTO_LIB{ "proto-lib" };
constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER_CONN_STR{ "conn-str" };
constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER_SENSOR_LIST_FILE{ "sensor-list-file" };
constexpr std::string_view CONFIG_GROUP_MSG_CONSUMER_SUBSCRIBE_TOPIC_LIST{ "subscribe-topic-list" };

// TILED_DISPLAY

constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY{ "tiled-display" };
constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY_ROWS{ "rows" };
constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY_COLUMNS{ "columns" };
constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY_WIDTH{ "width" };
constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY_HEIGHT{ "height" };
constexpr std::string_view CONFIG_GROUP_TILED_COMPUTE_HW{ "compute-hw" };
constexpr std::string_view CONFIG_GROUP_TILED_DISPLAY_BUFFER_POOL_SIZE{ "buffer-pool-size" };

// ANALYTICS

constexpr std::string_view CONFIG_GROUP_ANALYTICS{ "analytics" };
constexpr std::string_view CONFIG_GROUP_ANALYTICS_UNIQUE_ID{ "unique-id" };
constexpr std::string_view CONFIG_GROUP_ANALYTICS_DISTANCE{ "distance-between-lines" };
constexpr std::string_view CONFIG_GROUP_ANALYTICS_CONFIG_FILE{ "config-file" };
constexpr std::string_view CONFIG_GROUP_ANALYTICS_OUTPUT_PATH{"output-path"};
constexpr std::string_view CONFIG_GROUP_ANALYTICS_LP_MIN_LENGTH{"lp-min-length"};

// IMG_SAVE

constexpr std::string_view CONFIG_GROUP_IMG_SAVE{ "img-save" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_OUTPUT_FOLDER_PATH{ "output-folder-path" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_FULL_FRAME_IMG_SAVE{ "save-img-full-frame" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_CROPPED_OBJECT_IMG_SAVE{ "save-img-cropped-obj" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_CSV_TIME_RULES_PATH{ "frame-to-skip-rules-path" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_SECOND_TO_SKIP_INTERVAL{ "second-to-skip-interval" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_QUALITY{ "quality" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_MIN_CONFIDENCE{ "min-confidence" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_MAX_CONFIDENCE{ "max-confidence" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_MIN_BOX_WIDTH{ "min-box-width" };
constexpr std::string_view CONFIG_GROUP_IMG_SAVE_MIN_BOX_HEIGHT{ "min-box-height" };

// CONFIG_GROUP_SENSOR

constexpr std::string_view CONFIG_GROUP_SENSOR{ "sensor" };

// APP

constexpr std::string_view CONFIG_GROUP_APP{ "application" };
constexpr std::string_view CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT{ "enable-perf-measurement" };
constexpr std::string_view CONFIG_GROUP_APP_ENABLE_FILE_LOOP{ "file-loop" };
constexpr std::string_view CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL{ "perf-measurement-interval-sec" };
constexpr std::string_view CONFIG_GROUP_APP_OUTPUT_DIR{"output-dir"};
constexpr std::string_view CONFIG_GROUP_APP_GIE_OUTPUT_DIR{ "gie-kitti-output-dir" };
constexpr std::string_view CONFIG_GROUP_APP_GIE_TRACK_OUTPUT_DIR{ "kitti-track-output-dir" };
constexpr std::string_view CONFIG_GROUP_APP_REID_TRACK_OUTPUT_DIR{ "reid-track-output-dir" };
constexpr std::string_view CONFIG_GROUP_APP_GLOBAL_GPU_ID{ "global-gpu-id" };
constexpr std::string_view CONFIG_GROUP_APP_TERMINATED_TRACK_OUTPUT_DIR{ "terminated-track-output-dir" };
constexpr std::string_view CONFIG_GROUP_APP_SHADOW_TRACK_OUTPUT_DIR{ "shadow-track-output-dir" };

// TESTS

constexpr std::string_view CONFIG_GROUP_TESTS{ "tests" };
constexpr std::string_view CONFIG_GROUP_TESTS_PIPELINE_RECREATE_SEC{ "pipeline-recreate-sec" };

constexpr std::string_view CONFIG_GROUP_SOURCE_SGIE_BATCH_SIZE{ "sgie-batch-size" };

constexpr std::string_view SRC_CONFIG_KEY{ "src_config" };
const size_t SOURCE_RESET_INTERVAL_SEC{ 60 };

#endif // TADS_CONFIG_HPP
