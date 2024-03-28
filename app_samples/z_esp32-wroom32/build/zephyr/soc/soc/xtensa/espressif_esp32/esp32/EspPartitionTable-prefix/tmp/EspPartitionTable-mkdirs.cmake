# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/pdlsurya/ZephyrSDK/modules/hal/espressif/components/partition_table"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/esp-idf/build"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/tmp"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/src/EspPartitionTable-stamp"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/src"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/src/EspPartitionTable-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/src/EspPartitionTable-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/pdlsurya/Documents/ZephyrProject/app_samples/z_esp32-wroom32/build/zephyr/soc/soc/xtensa/espressif_esp32/esp32/EspPartitionTable-prefix/src/EspPartitionTable-stamp${cfgdir}") # cfgdir has leading slash
endif()
