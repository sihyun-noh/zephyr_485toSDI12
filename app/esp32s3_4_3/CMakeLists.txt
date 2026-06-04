# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE include)

# Add ADS1220_WE library (ported from Arduino)
target_sources(app PRIVATE src/ads1220_we.c)
# Remove old ADS1220 library
# target_sources(app PRIVATE lib/ADS1220/ADS1220.c)
# target_include_directories(app PRIVATE lib/ADS1220)
