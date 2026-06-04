# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)


FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# libqrencode 설정
set(QRENCODE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/libqrencode)
set(QRENCODE_SOURCES
    ${QRENCODE_SOURCE_DIR}/qrencode.c
    ${QRENCODE_SOURCE_DIR}/qrinput.c
    ${QRENCODE_SOURCE_DIR}/bitstream.c
    ${QRENCODE_SOURCE_DIR}/qrspec.c
    ${QRENCODE_SOURCE_DIR}/rsecc.c
    ${QRENCODE_SOURCE_DIR}/split.c
    ${QRENCODE_SOURCE_DIR}/mask.c
    ${QRENCODE_SOURCE_DIR}/mmask.c
    ${QRENCODE_SOURCE_DIR}/mqrspec.c
)

# libqrencode 헤더 경로 추가
zephyr_include_directories(${QRENCODE_SOURCE_DIR})
target_include_directories(app PRIVATE ${QRENCODE_SOURCE_DIR}/.)

# libqrencode 소스 추가
target_sources(app PRIVATE ${QRENCODE_SOURCES})



# include 디렉토리 추가
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)



