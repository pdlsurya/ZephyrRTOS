#ifndef _NRF24_RADIO
#define _NRF24_RADIO
#include "stdint.h"
// Register command
#define R_REGISTER 0x00
#define W_REGISTER 0x20

#define R_RX_PAYLOAD 0x61
#define W_TX_PAYLOAD 0xA0
#define FLUSH_TX 0xE1
#define FLUSH_RX 0xE2
#define REUSE_TX_PL 0xE3
#define R_RX_PL_WID 0x60
#define W_ACK_PAYLOAD 0xA8
#define W_TX_PAYLOAD_NO_ACK 0xB0
#define NOPP 0xFF

// register map
#define CONFIG 0x00
#define EN_AA 0x01
#define EN_RXADDR 0x02
#define SETUP_AW 0x03
#define SETUP_RETR 0x04
#define RF_CH 0x05
#define RF_SETUP 0x06
#define RADIO_STATUS 0x07
#define OBSERVE_TX 0x08
#define RPD 0x09
#define RX_ADDR_P0 0x0A
#define RX_ADDR_P1 0x0B
#define RX_ADDR_P2 0x0C
#define RX_ADDR_P3 0x0D
#define RX_ADDR_P4 0x0E
#define RX_ADDR_P5 0x0F
#define TX_ADDR 0x10
#define RX_PW_P0 0x11
#define RX_PW_P1 0x12
#define RX_PW_P2 0x13
#define RX_PW_P3 0x14
#define RX_PW_P4 0x15
#define RX_PW_P5 0x16
#define FIFO_STATUS 0x17
#define DYNPD 0x1C
#define FEATURE 0x1D

// register bit position
#define RX_DR 6
#define TX_DS 5
#define MAX_RT 4

// irq mask
#define TX_IRQ_MASK 0x40
#define RX_IRQ_MASK 0x20
#define MAX_RT_MASK 0x10

// define registers
typedef struct
{
  uint8_t REG_CONFIG;
  uint8_t REG_EN_AA;
  uint8_t REG_EN_RXADDR;
  uint8_t REG_SETUP_AW;
  uint8_t REG_SETUP_RETR;
  uint8_t REG_RF_CH;
  uint8_t REG_RF_SETUP;
  uint8_t REG_STATUS;
  uint8_t REG_OBSERVE_TX;
  uint8_t REG_RPD;
  uint8_t REG_RX_ADDR_PX[5];
  uint8_t REG_TX_ADDR[5];
  uint8_t REG_RX_PW_PX;
  uint8_t REG_FIFO_STATUS;
  uint8_t REG_DYNPD;
  uint8_t REG_FEATURE;

} nrf24_register_t;

typedef enum
{
  DR_1MBPS,
  DR_2MBPS,
  DR_250KBPS
} nrf24_data_rate_t;

typedef enum
{
  TX_POWER_MAX,
  TX_POWER_MODERATE,
  TX_POWER_MIN
} nrf24_tx_power_t;

typedef enum
{
  TX_MODE,
  RX_MODE
} radio_mode_t;

bool nrf24_init();
void nrf24_enable_crc();
void nrf24_disable_crc();
void nrf24_set_tx_address(const uint8_t *address);
void nrf24_set_rx_address(uint8_t pipe, const uint8_t *address);
void nrf24_set_tx_power(nrf24_tx_power_t power_level);
void nrf24_set_data_rate(nrf24_data_rate_t data_rate);
void nrf24_set_channel(uint8_t channel);
void nrf24_set_address_width(uint8_t width);
void nrf24_set_mode(radio_mode_t mode);
bool nrf24_available(uint8_t *pipe);
void nrf24_rx(uint8_t *buffer, uint8_t length);
bool nrf24_tx(uint8_t *buffer, uint8_t length);
void nrf24_tx_no_ack(uint8_t *buffer, uint8_t length);
void nrf24_set_payload_size(uint8_t size);
void nrf24_clear_irq_flags(uint8_t flags);
void nrf24_set_arc_count(uint8_t count);
void nrf24_set_retransmit_delay(uint8_t delay_level);
void nrf24_enable_rx_pipe(uint8_t pipe);
uint8_t nrf24_get_rx_payload_size();
void nrf24_power_up();
void nrf24_set_auto_ack(bool set_value);
void nrf24_flush_tx();
void nrf24_flush_rx();
void nrf24_enable_dynamic_ack();
#endif
