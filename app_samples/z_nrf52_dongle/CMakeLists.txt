# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_ZEPHYR_MODULES
 "/users/pdlsurya/documents/EmbeddedProjects/ZephyrRTOS")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(z_nrf52_dongle)

target_sources(app PRIVATE src/main.c
                           src/my_timer/my_timer.c
                           src/led_indication/led_indication.c)
target_include_directories(app PRIVATE 
                                       src/my_timer
                                       src/led_indication )

