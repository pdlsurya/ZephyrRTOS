# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/pdlsurya/ZephyrSDK/modules/hal/espressif/components/bootloader/subproject"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/build/bootloader"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/tmp"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/src/EspIdfBootloader-stamp"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/src"
  "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/src/EspIdfBootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/src/EspIdfBootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/pdlsurya/Documents/ZephyrProject/app_samples/ble_peripheral/build/esp-idf/src/EspIdfBootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
