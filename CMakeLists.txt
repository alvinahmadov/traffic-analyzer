cmake_minimum_required(VERSION 3.20)

set(CUDA_VER 12.4)
set(CUDAToolkit_ROOT "/usr/local/cuda-${CUDA_VER}")
set(CMAKE_CUDA_COMPILER "${CUDAToolkit_ROOT}/bin/nvcc")
set(CUDA_LIBRARY_DIRS "${CUDAToolkit_ROOT}/lib64")
set(NVDS_ROOT_DIR "/opt/nvidia/deepstream/deepstream")
set(NVDS_INCLUDE_DIRS "${NVDS_ROOT_DIR}/sources/includes")
set(NVDS_LIBRARY_DIRS "${NVDS_ROOT_DIR}/lib")
set(CMAKE_CUDA_RUNTIME_LIBRARY "Shared")

project(tads LANGUAGES CXX CUDA)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(NVDSINFER_YOLO_CUSTOM_LIB nvdsinfer_custom_impl_yolo)
set(NVDSINFER_LPR_CUSTOM_LIB nvdsinfer_custom_impl_lpr)
option(BUILD_YOLO_CUSTOM "Build yolo nvdsinfer custom library" ON)
option(BUILD_LPR_CUSTOM "Build lpr nvdsinfer custom library" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

include(FindCUDAToolkit)

find_package(PkgConfig)
find_package(Threads)
find_package(fmt)
find_package(X11 REQUIRED)
find_package(OpenCV)

set(CMAKE_CXX_FLAGS "-Wno-attributes -Wno-write-strings")

set(NVDS_LINK_LIBRARIES
        -lcustom_audioimpl
        -lcustom_videoimpl
        -lgstnvcustomhelper
        -lnvbufsurface
        -lnvbufsurftransform
        -lnvds_amqp_proto
        -lnvds_batch_jpegenc
        -lnvdsbufferpool
        -lnvds_csvparser
        -lnvds_custom_sequence_preprocess
        -lnvds_dsanalytics
#        -lnvdsgst_bufferpool
        -lnvdsgst_customhelper
        -lnvdsgst_helper
        -lnvdsgst_inferbase
        -lnvdsgst_meta
        -lnvdsgst_smartrecord
        -lnvdsgst_tensor
        -lnvds_infer
        -lnvds_infercustomparser
        -lnvds_infer_custom_parser_audio
        -lnvds_inferlogger
        -lnvds_infer_server
        -lnvds_inferutils
        -lnvds_kafka_proto
        -lnvds_logger
        -lnvds_mem_allocator
        -lnvds_meta
        -lnvds_mqtt_proto
        -lnvds_msgbroker
        -lnvds_msgconv
        -lnvds_msgconv_audio
        -lnvds_nmos
        -lnvds_nvmultiobjecttracker
        -lnvds_nvtxhelper
        -lnvds_opticalflow_dgpu
        -lnvds_osd
        -lnvds_redis_proto
        -lnvds_rest_server
        -lnvds_tritoninferfilter
        -lnvds_utils
        -lnvds_yml_parser
#        -lnvv4l2
        -lnvvpi
        -lpostprocess_impl
)

pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GST_RTSP REQUIRED gstreamer-rtsp-1.0)
pkg_check_modules(GST_RTSP_SERVER REQUIRED gstreamer-rtsp-server-1.0)
pkg_check_modules(GLIB_JSON REQUIRED json-glib-1.0)
pkg_check_modules(LIBYAML REQUIRED yaml-cpp)

link_directories(
        ${GLIB_LIBRARY_DIRS}
        ${GST_LIBRARY_DIRS}
        ${GST_APP_LIBRARY_DIRS}
        ${GST_VIDEO_LIBRARY_DIRS}
        ${GST_BASE_LIBRARY_DIRS}
        ${GST_RTSP_LIBRARY_DIRS}
        ${GST_RTSP_SERVER_LIBRARY_DIRS}
        ${CUDA_LIBRARY_DIRS}
        ${NVDS_LIBRARY_DIRS}
)
include_directories(
        ${GLIB_INCLUDE_DIRS}
        ${GST_INCLUDE_DIRS}
        ${CUDA_INCLUDE_DIRS}
        ${NVDS_INCLUDE_DIRS}
)
link_libraries(
        fmt::fmt
        CUDA::toolkit
        CUDA::cudart
        CUDA::cupti
        CUDA::nvjpeg
        CUDA::cublas
        ${NVDS_LINK_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${GST_LIBRARIES}
)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SOURCES)

set(NVDSINFER_LIBRARIES
        -lnvinfer_plugin
        -lnvinfer
        -lnvparsers
        -lnvonnxparser
        -lstdc++fs
)

if (${BUILD_YOLO_CUSTOM})
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/yolo/layers yolo_layers)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/yolo yolo_src)

    add_library(${NVDSINFER_YOLO_CUSTOM_LIB} SHARED ${yolo_layers} ${yolo_src})
    target_include_directories(${NVDSINFER_YOLO_CUSTOM_LIB}
            PUBLIC
            include/yolo
            ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(${NVDSINFER_YOLO_CUSTOM_LIB}
            PRIVATE
            ${OpenCV_LIBRARIES}
            ${NVDSINFER_LIBRARIES}
            ${NVDS_LINK_LIBRARIES}
    )
    set_target_properties(${NVDSINFER_YOLO_CUSTOM_LIB} PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    set_target_properties(${NVDSINFER_YOLO_CUSTOM_LIB} PROPERTIES CUDA_ARCHITECTURES "50;72")
    message(STATUS "YOLO enabled for project")
else ()
    message(STATUS "YOLO disabled for project")
    set(NVDSINFER_YOLO_CUSTOM_LIB)
endif ()

if (${BUILD_LPR_CUSTOM})
    add_definitions(-DOPENCV=1)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/lpr lpr_src)

    add_library(${NVDSINFER_LPR_CUSTOM_LIB} SHARED ${lpr_src})
    target_include_directories(${NVDSINFER_LPR_CUSTOM_LIB} PUBLIC
            ${GLIB_INCLUDE_DIRS}
            ${GLIB_INCLUDE_DIRS}
            ${GST_INCLUDE_DIRS}
    )
    target_link_libraries(${NVDSINFER_LPR_CUSTOM_LIB} PRIVATE
            ${NVDSINFER_LIBRARIES}
            ${NVDS_LINK_LIBRARIES}
            ${GLIB_LIBRARIES}
            ${GST_LIBRARIES})
    set_target_properties(${NVDSINFER_LPR_CUSTOM_LIB} PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    message(STATUS "LPR enabled for project")
else ()
    message(STATUS "LPR disabled for project")
    set(NVDSINFER_LPR_CUSTOM_LIB)
endif ()

add_executable(${PROJECT_NAME} main.cpp ${SOURCES})
add_dependencies(${PROJECT_NAME} ${NVDSINFER_YOLO_CUSTOM_LIB} ${NVDSINFER_YOLO_CUSTOM_LIB})

target_include_directories(
        ${PROJECT_NAME} PUBLIC
        ${GST_APP_INCLUDE_DIRS}
        ${GST_VIDEO_INCLUDE_DIRS}
        ${GST_BASE_INCLUDE_DIRS}
        ${GST_RTSP_INCLUDE_DIRS}
        ${GST_RTSP_SERVER_INCLUDE_DIRS}
        ${GLIB_JSON_INCLUDE_DIRS}
        ${LIBYAML_INCLUDE_DIRS}
        ${X11_INCLUDE_DIR}
        include
)
target_link_libraries(
        ${PROJECT_NAME} PUBLIC
#        ${GLIB_LIBRARIES}
#        ${GST_LIBRARIES}
        ${GST_APP_LIBRARIES}
        ${GST_VIDEO_LIBRARIES}
        ${GST_BASE_LIBRARIES}
        ${GST_RTSP_LIBRARIES}
        ${GST_RTSP_SERVER_LIBRARIES}
        ${GLIB_JSON_LINK_LIBRARIES}
        ${X11_LIBRARIES}
        ${LIBYAML_LIBRARIES}
        ${NVDSINFER_YOLO_CUSTOM_LIB}
        ${NVDSINFER_LPR_CUSTOM_LIB}
)

set_target_properties(${PROJECT_NAME} PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
