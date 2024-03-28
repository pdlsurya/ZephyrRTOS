#ifndef __SD_DRIVER_H
#define __SD_DRIVER_H
typedef enum{
   SD_READY, SD_INIT_SUCCESS, SD_INIT_ERROR, SD_READ_SUCCESS, SD_READ_ERROR,SD_WRITE_SUCCESS, SD_WRITE_ERROR
}sd_ret_t;

uint8_t SD_init();

uint8_t SD_readSector(uint32_t SecAddr, uint8_t *buf);

uint8_t SD_writeSector(uint32_t SecAddr, uint8_t* buf);

uint8_t SD_readMultipleSecStart(uint32_t start_addr);

sd_ret_t SD_readMultipleSec(uint8_t *buff);

void SD_readMultipleSecStop();

void SD_readMultipleSecStop();

uint8_t SD_writeMultipleBlock(uint32_t start_addr, uint8_t blockCnt);

#endif