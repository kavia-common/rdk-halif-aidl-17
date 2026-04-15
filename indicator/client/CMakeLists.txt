#** *****************************************************************************
# *
# * If not stated otherwise in this file or this component's LICENSE file the
# * following copyright and licenses apply:
# *
# * Copyright 2026 RDK Management
# *
# * Licensed under the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# * http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *
#** *****************************************************************************

cmake_minimum_required(VERSION 3.8)

project(IndicatorClient LANGUAGES CXX)

add_library(rdk_hal_indicator_client STATIC
    src/IndicatorClient.cpp
)

target_compile_features(rdk_hal_indicator_client PUBLIC cxx_std_17)

target_include_directories(rdk_hal_indicator_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Consumers must ensure generated AIDL headers are on the include path.
# With this repository's build tooling, generated headers are in:
#   ${CMAKE_SOURCE_DIR}/gen/${AIDL_TARGET}/${AIDL_SRC_VERSION}/h
#
# We add that include directory when building in-tree with the standard variables.
if (DEFINED AIDL_GEN_DIR)
    target_include_directories(rdk_hal_indicator_client
        PUBLIC
            ${AIDL_GEN_DIR}/h
    )
endif()

# NDK binder library linkage is platform-specific; keep it optional here.
# A consumer can link binder_ndk and related libs as appropriate.
#
# Example on platforms with libbinder_ndk:
#   target_link_libraries(rdk_hal_indicator_client PUBLIC binder_ndk)
