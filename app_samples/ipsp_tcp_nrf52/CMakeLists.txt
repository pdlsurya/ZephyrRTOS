# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
list(APPEND EXTRA_ZEPHYR_MODULES
 "/users/pdlsurya/documents/EmbeddedProjects/ZephyrRTOS")

project(ipsp)

target_sources(app PRIVATE
  src/main.c
)
