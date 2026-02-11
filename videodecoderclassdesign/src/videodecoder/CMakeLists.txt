cmake_minimum_required(VERSION 3.16)

# Standalone stub library for the Video Decoder class design.
# This does NOT integrate with the repo's root AIDL build; it is an optional
# build target to validate that the skeleton compiles.

project(VideoDecoderClassDesignStub LANGUAGES CXX)

add_library(videodecoder_classdesign_stub
    src/AVBufferAdapter.cpp
    src/VideoDecoder.cpp
    src/VideoDecoderController.cpp
    src/VideoDecoderManager.cpp
)

target_include_directories(videodecoder_classdesign_stub
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(videodecoder_classdesign_stub PUBLIC cxx_std_17)

# Keep warnings reasonable for a skeleton.
if (MSVC)
    target_compile_options(videodecoder_classdesign_stub PRIVATE /W4)
else()
    target_compile_options(videodecoder_classdesign_stub PRIVATE -Wall -Wextra -Wpedantic)
endif()
