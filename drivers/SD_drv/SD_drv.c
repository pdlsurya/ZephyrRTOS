/**
 * @file SD_driver.c
 * @author Surya Poudel
 * @brief  Driver for SD/uSD card connected to SPI bus
 * @version 0.1
 * @date 2023-06-14
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "SD_drv.h"

#define CMD0 0
#define CMD0_ARG 0x00000000
#define CMD0_CRC 0x94

// SEND IF_COND
#define CMD8 8
#define CMD8_ARG 0x000001AA
#define CMD8_CRC 0x86 //(1000011 << 1)

// READ CSD
#define CMD9 9
#define CMD9_ARG 0x00000000
#define CMD9_CRC 0x00

// Read OCR
#define CMD58 58
#define CMD58_ARG 0x00000000
#define CMD58_CRC 0x00

#define CMD55 55
#define CMD55_ARG 0x00000000
#define CMD55_CRC 0x00

#define ACMD41 41
#define ACMD41_ARG 0x40000000
#define ACMD41_CRC 0x00

// Read Single Block
#define CMD17 17
#define CMD17_CRC 0x95
#define SD_MAX_READ_ATTEMPTS 4500

// Write Single Block
#define CMD24 24
#define CMD24_CRC 0x00
#define SD_MAX_WRITE_ATTEMPTS 3907

// Read Multiple Block
#define CMD18 18
#define CMD18_CRC 0x00

// STOP_MULTIPLE_READ
#define CMD12 12
#define CMD12_ARG 0x00000000
#define CMD12_CRC 0x00

// Write Multiple Block
#define CMD25 25
#define CMD25_CRC 0x00

#define PARAM_ERROR(X) X & 0b01000000
#define ADDR_ERROR(X) X & 0b00100000
#define ERASE_SEQ_ERROR(X) X & 0b00010000
#define CRC_ERROR(X) X & 0b00001000
#define ILLEGAL_CMD(X) X & 0b00000100
#define ERASE_RESET(X) X & 0b00000010
#define IN_IDLE(X) X & 0b00000001

#define CMD_VER(X) ((X >> 4) & 0x0F)
#define VOL_ACC(X) (X & 0x1F)

#define VOLTAGE_ACC_27_33 0b00000001
#define VOLTAGE_ACC_LOW 0b00000010
#define VOLTAGE_ACC_RES1 0b00000100
#define VOLTAGE_ACC_RES2 0b00001000

#define POWER_UP_STATUS(X) X & 0x80
#define CCS_VAL(X) X & 0x40
#define VDD_2728(X) X & 0b10000000
#define VDD_2829(X) X & 0b00000001
#define VDD_2930(X) X & 0b00000010
#define VDD_3031(X) X & 0b00000100
#define VDD_3132(X) X & 0b00001000
#define VDD_3233(X) X & 0b00010000
#define VDD_3334(X) X & 0b00100000
#define VDD_3435(X) X & 0b01000000
#define VDD_3536(X) X & 0b10000000

#define SD_TOKEN_OOR(X) X & 0b00001000
#define SD_TOKEN_CECC(X) X & 0b00000100
#define SD_TOKEN_CC(X) X & 0b00000010
#define SD_TOKEN_ERROR(X) X & 0b00000001

#define SD_START_TOKEN 0xFE
#define SD_BLOCK_LEN 512

struct spi_dt_spec spi_drv_spec = SPI_DT_SPEC_GET(DT_NODELABEL(microsd), (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8) | SPI_HOLD_ON_CS | SPI_LOCK_ON), 0);

uint8_t spi_tx_buf[512];

void SD_spi_transceive(uint8_t *tx_buffer, uint8_t *rx_buffer, uint16_t len)
{

    struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = len};

    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1};

    if (rx_buffer != NULL)
    {
        struct spi_buf rx_buf = {
            .buf = rx_buffer,
            .len = len};

        struct spi_buf_set rx_bufs = {
            .buffers = &rx_buf,
            .count = 1};

        spi_transceive_dt(&spi_drv_spec, &tx_bufs, &rx_bufs);
    }
    else
        spi_write_dt(&spi_drv_spec, &tx_bufs);
}

void SD_powerUpSeq()
{
    spi_release_dt(&spi_drv_spec);
    memset(spi_tx_buf, 0xff, 512);

    SD_spi_transceive(spi_tx_buf, NULL, 10);

    spi_release_dt(&spi_drv_spec);
}

void SD_command(uint8_t cmd, uint32_t arg, uint8_t crc)
{

    uint8_t cmd_buf[7] = {0};
    cmd_buf[0] = 0xFF;
    cmd_buf[1] = cmd | 0x40;
    cmd_buf[2] = (uint8_t)(arg >> 24);
    cmd_buf[3] = (uint8_t)(arg >> 16);
    cmd_buf[4] = (uint8_t)(arg >> 8);
    cmd_buf[5] = (uint8_t)(arg);
    cmd_buf[6] = crc | 0x01;

    SD_spi_transceive(cmd_buf, NULL, 7);
}

uint8_t SD_readRes1()
{
    uint8_t i = 0, res1 = 0xFF;
    // keep polling until actual data received
    do
    {
        SD_spi_transceive(spi_tx_buf, &res1, 1);
        i++;
        // if no data received for 8 bytes, break
        if (i > 8)
            break;
    } while (res1 == 0xFF);

    return res1;
}

uint8_t SD_goIdleState()
{

    // send CMD0
    SD_command(CMD0, CMD0_ARG, CMD0_CRC);

    // read response
    uint8_t res1 = SD_readRes1();

    // Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
    return res1;
}

void SD_readRes3_7(uint8_t *res)
{
    // read response 1 in R7
    res[0] = SD_readRes1();

    // if error reading R1, return
    if (res[0] > 1)
        return;
    SD_spi_transceive(spi_tx_buf, &res[1], 4);
}

void SD_sendIfCond(uint8_t *res)
{

    // send CMD8
    SD_command(CMD8, CMD8_ARG, CMD8_CRC);

    // read response
    SD_readRes3_7(res);

    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
}

void SD_readOCR(uint8_t *res)
{

    // send CMD58
    SD_command(CMD58, CMD58_ARG, CMD58_CRC);

    // read response
    SD_readRes3_7(res);

    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
}

uint8_t SD_sendApp()
{

    // send CMD0
    SD_command(CMD55, CMD55_ARG, CMD55_CRC);

    // read response
    uint8_t res1 = SD_readRes1();

    // Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
    return res1;
}

uint8_t SD_sendOpCond()
{

    // send CMD0
    SD_command(ACMD41, ACMD41_ARG, ACMD41_CRC);

    // read response
    uint8_t res1 = SD_readRes1();

    // Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
    return res1;
}

void SD_printR1(uint8_t res)
{
    if (res & 0b10000000)
    {
        printk("\tError: MSB = 1\r");
        return;
    }
    if (res == 0)
    {
        printk("\t Card Ready \r");
        return;
    }
    if (PARAM_ERROR(res))
        printk("\tParameter Error\r");
    if (ADDR_ERROR(res))
        printk("\tAddress Error\r");
    if (ERASE_SEQ_ERROR(res))
        printk("\tErase Seq Error\r");
    if (CRC_ERROR(res))
        printk("\tCRC Error\r");
    if (ILLEGAL_CMD(res))
        printk("\tIllegal Cmd\r");
    if (ERASE_RESET(res))
        printk("\tErase Rst Error\r");
    if (IN_IDLE(res))
        printk("Idle State\r");
}

void SD_printR7(uint8_t *res)
{
    SD_printR1(res[0]);

    if (res[0] > 1)
        return;

    printk("\tCommand Version: %x", res[1]);

    printk("\tVoltage Accepted: ");
    if (VOL_ACC(res[3]) == VOLTAGE_ACC_27_33)
        printk("2.7-3.6V\r");
    else if (VOL_ACC(res[3]) == VOLTAGE_ACC_LOW)
        printk("LOW VOLTAGE\r");
    else if (VOL_ACC(res[3]) == VOLTAGE_ACC_RES1)
        printk("RESERVED\r");
    else if (VOL_ACC(res[3]) == VOLTAGE_ACC_RES2)
        printk("RESERVED\r");
    else
        printk("NOT DEFINED");

    printk("\tEcho: %x", res[4]);
}

void SD_printR3(uint8_t *res)
{
    SD_printR1(res[0]);

    if (res[0] > 1)
        return;

    printk("\tCard Power Up Status: ");
    if (POWER_UP_STATUS(res[1]))
    {
        printk("READY\r\n");
        printk("\tCCS Status: ");
        if (CCS_VAL(res[1]))
        {
            printk("1\r\n");
        }
        else
            printk("0\r\n");
    }
    else
    {
        printk("BUSY\r\n");
    }

    printk("\tVDD Window: ");
    if (VDD_2728(res[3]))
        printk("2.7-2.8, ");
    if (VDD_2829(res[2]))
        printk("2.8-2.9, ");
    if (VDD_2930(res[2]))
        printk("2.9-3.0, ");
    if (VDD_3031(res[2]))
        printk("3.0-3.1, ");
    if (VDD_3132(res[2]))
        printk("3.1-3.2, ");
    if (VDD_3233(res[2]))
        printk("3.2-3.3, ");
    if (VDD_3334(res[2]))
        printk("3.3-3.4, ");
    if (VDD_3435(res[2]))
        printk("3.4-3.5, ");
    if (VDD_3536(res[2]))
        printk("3.5-3.6");
    printk("\r\n");
}

void SD_printDataErrToken(uint8_t token)
{
    if (SD_TOKEN_OOR(token))
        printk("\tData out of range\r\n");
    if (SD_TOKEN_CECC(token))
        printk("\tCard ECC failed\r\n");
    if (SD_TOKEN_CC(token))
        printk("\tCC Error\r\n");
    if (SD_TOKEN_ERROR(token))
        printk("\tError\r\n");
}

uint8_t SD_read_start(uint8_t *buf, uint16_t read_len, uint8_t *token)
{
    uint8_t res1, read = 0xFF;
    uint16_t readAttempts;

    // read R1
    res1 = SD_readRes1();

    // if response received from card
    if (res1 == SD_READY)
    {
        // wait for a response token (timeout = 100ms)
        readAttempts = 0;

        do
        {
            SD_spi_transceive(spi_tx_buf, &read, 1);
            if (readAttempts == SD_MAX_READ_ATTEMPTS)
                break;
            readAttempts++;
        } while (read != 0xFE);

        // if response token is 0xFE
        if (read == 0xFE)
        {
            // read 512 byte block
            SD_spi_transceive(spi_tx_buf, buf, read_len);
            // read 16-bit CRC and a end byte
            SD_spi_transceive(spi_tx_buf, NULL, 2);
        }

        // set token to card response
        *token = read;
    }

    return res1;
}

uint8_t SD_readCSD(uint8_t *CSD)
{
    uint8_t token, res1;

    // send CMD0
    SD_command(CMD9, CMD9_ARG, CMD9_CRC);

    res1 = SD_read_start(CSD, 16, &token);

    // Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);

    if (res1 == SD_READY)
    {
        // if error token received
        if (!(token & 0xF0))
        {
            // SD_printDataErrToken(token);
            return SD_READ_ERROR;
        }
        else if (token == 0xFF)
        {
            printk("Read Timeout\r\n");
            return SD_READ_ERROR;
        }
        return SD_READ_SUCCESS;
    }
    else
    {
        // SD_printR1(res1);
        return SD_READ_ERROR;
    }
}

uint8_t SD_readSingleBlock(uint32_t addr, uint8_t *buf, uint8_t *token)
{
    // set token to none
    *token = 0xFF;

    // send CMD17
    SD_command(CMD17, addr, CMD17_CRC);
    uint8_t res1 = SD_read_start(buf, SD_BLOCK_LEN, token);

    // Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);

    return res1;
}

uint8_t SD_readSector(uint32_t addr, uint8_t *buf)
{
    uint8_t res1, token;

    res1 = SD_readSingleBlock(addr, buf, &token);
    if (res1 == SD_READY)
    {
        // if error token received
        if (!(token & 0xF0))
        {
            SD_printDataErrToken(token);
            return SD_READ_ERROR;
        }
        else if (token == 0xFF)
        {
            printk("Read Timeout\r\n");
            return SD_READ_ERROR;
        }
        return SD_READ_SUCCESS;
    }
    else
    {
        // SD_printR1(res1);
        return SD_READ_ERROR;
    }
}

uint8_t _writeSingleBlock(uint32_t addr, uint8_t *buf, uint8_t *token)
{
    uint8_t writeAttempts, read, res1;

    // set token to none
    *token = 0xFF;

    // send CMD24
    SD_command(CMD24, addr, CMD24_CRC);

    // read response
    res1 = SD_readRes1();

    // if no error
    if (res1 == SD_READY)
    {
        uint8_t start_token = SD_START_TOKEN;
        // send start token
        SD_spi_transceive(&start_token, NULL, 1);

        // write buffer to card
        SD_spi_transceive(spi_tx_buf, buf, SD_BLOCK_LEN);
        // wait for a response (timeout = 250ms)
        writeAttempts = 0;

        while (writeAttempts != SD_MAX_WRITE_ATTEMPTS)
        {
            SD_spi_transceive(spi_tx_buf, &read, 1);
            if (read != 0xFF)
                break;
            writeAttempts++;
        }
        // if data accepted
        if ((read & 0x1F) == 0x05)
        {
            // set token to data accepted
            *token = 0x05;

            // wait for write to finish (timeout = 250ms)
            writeAttempts = 0;
            read = 0;
            do
            {
                SD_spi_transceive(spi_tx_buf, &read, 1);
                if (writeAttempts == SD_MAX_WRITE_ATTEMPTS)
                {
                    *token = 0x00;
                    break;
                }
                writeAttempts++;
            } while (read == 0);
        }
    }

    // send a padding byte and Release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);

    return res1;
}

uint8_t SD_writeSector(uint32_t addr, uint8_t *buf)
{
    uint8_t token, res1;
    res1 = _writeSingleBlock(addr, buf, &token);

    if (res1 == SD_READY)
    {
        if (token == 0x05)
            return SD_WRITE_SUCCESS;
        else if (token == 0xFF || token == 0x00)
            return SD_WRITE_ERROR;
    }
    else
    {
        // SD_printR1(res1);
        return SD_WRITE_ERROR;
    }
    return SD_WRITE_ERROR;
}

uint8_t SD_readMultipleSecStart(uint32_t start_addr)
{
    uint8_t res1;

    // send CMD24
    SD_command(CMD18, start_addr, CMD18_CRC);

    // read response
    res1 = SD_readRes1();

    return res1;
}

sd_ret_t SD_readMultipleSec(uint8_t *buff)
{
    uint8_t read = 0xFF;
    uint32_t readAttempts;

    // wait for a response token (timeout = 100ms)
    readAttempts = 0;

    do
    {
        SD_spi_transceive(spi_tx_buf, &read, 1);
        if (readAttempts == SD_MAX_READ_ATTEMPTS)
            break;
        readAttempts++;
    } while (read != 0xFE);

    // if response token is 0xFE
    if (read == 0xFE)
    {
        SD_spi_transceive(spi_tx_buf, buff, SD_BLOCK_LEN);

        // read 16-bit CRC and end byte
        SD_spi_transceive(spi_tx_buf, NULL, 3);
    }

    if (!(read & 0xF0))
    {
        SD_printDataErrToken(read);
        return SD_READ_ERROR;
    }
    else if (read == 0xFF)
    {
        printk("Read Timeout\r\n");
        return SD_READ_ERROR;
    }
    return SD_READ_SUCCESS;
}

void SD_readMultipleSecStop()
{
    SD_command(CMD12, CMD12_ARG, CMD12_CRC);
    uint8_t read = 0;

    do
    {
        SD_spi_transceive(spi_tx_buf, &read, 1);
    } while (read == 0);

    // release SPI bus
    SD_spi_transceive(spi_tx_buf, NULL, 1);
    spi_release_dt(&spi_drv_spec);
}

uint8_t SD_init()
{
    printk("Using custom SDHC driver by SURYA POUDEL\n");
    // uint8_t csd_reg[16];
    uint8_t res[5], cmdAttempts = 0;

    if (!spi_is_ready_dt(&spi_drv_spec))
    {
        return SD_INIT_ERROR;
    }

    SD_powerUpSeq();
    // command card to idle
    while ((res[0] = SD_goIdleState()) != 0x01)
    {
        cmdAttempts++;
        if (cmdAttempts > 50)
        {
            if (res[0] == 0)
            {
                printk("Card Not Found!\n");
            }
            // SD_printR1(res[0]);
            return SD_INIT_ERROR;
        }
    }
    // send interface conditions
    SD_sendIfCond(res);
    if (res[0] != 0x01)
    {
        // SD_printR1(res[0]);
        return SD_INIT_ERROR;
    }

    // check echo pattern
    if (res[4] != 0xAA)
    {
        // SD_printR7(res);
        return SD_INIT_ERROR;
    }

    // attempt to initialize card
    cmdAttempts = 0;
    do
    {
        if (cmdAttempts > 100)
            return SD_INIT_ERROR;

        // send app cmd
        res[0] = SD_sendApp();

        // if no error in response
        if (res[0] < 2)
        {
            res[0] = SD_sendOpCond();
        }

        // wait
        k_sleep(K_MSEC(10));

        cmdAttempts++;
    } while (res[0] != SD_READY);

    // read OCR
    SD_readOCR(res);
    // check card is ready
    if (!(res[1] & 0x80))
    {
        // SD_printR3(res);
        return SD_INIT_ERROR;
    }
    else
    {
        if (res[1] & 0x40)
            printk("Card Type: SDHC \n");
    }
    return SD_INIT_SUCCESS;
}

/*
uint8_t _writeMultipleBlock(uint32_t start_addr, uint8_t blockCnt, uint8_t *token)
{
    uint8_t writeAttempts, read, res1;

    // set token to none
    *token = 0xFF;
    // send CMD25
    SD_command(CMD25, start_addr, CMD25_CRC);

    // read response
    res1 = SD_readRes1();

    // if no error
    if (res1 == SD_READY)
    {
        printk("Enter text:");
        while (blockCnt--)
        {
            uint8_t write_buff[512] = {0};
            int i = 0;
            while (i < 512)
            {
                while (!Serial.available())
                    ;

                write_buff[i++] = Serial.read();
            }

            // send start token
            SPI.transfer(0xFC);

            // write buffer to card
            for (uint16_t i = 0; i < SD_BLOCK_LEN; i++)
                SPI.transfer(write_buff[i]);
            // wait for a response (timeout = 250ms)
            writeAttempts = 0;

            while (writeAttempts != SD_MAX_WRITE_ATTEMPTS)
            {
                if ((read = SPI.transfer(0xFF)) != 0xFF)
                    break;
                writeAttempts++;
            }
            // if data accepted
            if ((read & 0x1F) == 0x05)
            {
                // set token to data accepted
                *token = 0x05;

                // wait for write to finish (timeout = 250ms)
                writeAttempts = 0;
                while (SPI.transfer(0xFF) == 0x00)
                {
                    if (writeAttempts == SD_MAX_WRITE_ATTEMPTS)
                    {
                        *token = 0x00;
                        break;
                    }
                    writeAttempts++;
                }
                if (writeAttempts < SD_MAX_WRITE_ATTEMPTS)
                {
                    printk("Block write success!");
                }
            }
        }
        // stop writing
        SPI.transfer(0xFD);
    }

    // deassert chip select
     SD_spi_transceive(spi_tx_buf, NULL, 1);
spi_release_dt(&spi_drv_spec);

    return res1;
}

uint8_t SD_writeMultipleBlock(uint32_t start_addr, uint8_t blockCnt)
{

    uint8_t token, res1;
    res1 = _writeMultipleBlock(start_addr, blockCnt, &token);

    if (res1 == SD_READY)
    {
        if (token == 0x05)
            return SD_WRITE_SUCCESS;
        else if (token == 0xFF || token == 0x00)
            return SD_WRITE_ERROR;
    }

    SD_printR1(res1);
    return SD_WRITE_ERROR;
}

void writeMultipleSecStop(){
    uint8_t stop_token=0xFD;
     SD_spi_transceive(&stop_token,NULL, 1);
}
*/
