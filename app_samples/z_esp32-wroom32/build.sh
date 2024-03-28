source ~/zephyrSDK/zephyr/zephyr-env.sh
west build -p always -b esp32_devkitc_wroom
west flash
