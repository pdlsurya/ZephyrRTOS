# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES
 "/users/pdlsurya/documents/ZephyrProject")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ipsp)

target_sources(app PRIVATE
  src/main.c
  src/net_ble/net_ble.c
  src/ntp_clock/ntp_clock.c
)

target_include_directories(app PRIVATE 
src/net_ble
src/ntp_clock)
