# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES
 "/users/pdlsurya/documents/EmbeddedProjects/ZephyrRTOS")


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(z_esp32-wroom32)

target_sources(app PRIVATE src/main.c)





