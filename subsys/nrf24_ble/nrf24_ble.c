/**
 * @file nrf24_ble.c
 * @author Surya Poudel(poudel.surya2011@gmail.com)
 * @brief BLE advertisement driver for nRF24 series modules.
 * @version 0.1
 * @date 2023-07-07
 * 
 * @copyright Copyright(c) 2023, Surya Poudel
 *  SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <stdint.h>
#include "nrf24_ble.h"

uint8_t y = 16;
uint8_t refresh;
uint8_t device_index;
bool match;
uint8_t device_id[4];

nrf24_ble_instance_t ble_instance;
uint8_t *pdu_service_data_ptr;
const uint8_t channel[3] = {37, 38, 39};  // logical BTLE channel number (37-39)
const uint8_t frequency[3] = {2, 26, 80}; // physical frequency (2400+x MHz)
const uint8_t access_address[4] = {0x71, 0x91, 0x7D, 0x6B};
const uint8_t mac_address[6] = {0xfd, 0x5c, 0x1a, 0xa8, 0xef, 0xca};

uint32_t get_device_sum(uint8_t *device_name)
{
  return (uint32_t)(device_name[6]);
}

void reverse_bit_order(uint8_t *data, uint8_t length)
{

  while (length--)
  {
    uint8_t res = 0;

    if (*data & 0x80)
      res |= 0x01;
    if (*data & 0x40)
      res |= 0x02;
    if (*data & 0x20)
      res |= 0x04;
    if (*data & 0x10)
      res |= 0x08;
    if (*data & 0x08)
      res |= 0x10;
    if (*data & 0x04)
      res |= 0x20;
    if (*data & 0x02)
      res |= 0x40;
    if (*data & 0x01)
      res |= 0x80;

    *(data++) = res;
  }
}

void ble_set_mode(ble_mode_t mode)
{
  nrf24_flush_tx();
  nrf24_flush_rx();
  ble_instance.ble_mode = mode;
  switch (mode)
  {
  case BLE_MODE_ADVERTISE:
    nrf24_set_mode(TX_MODE);
    break;

  case BLE_MODE_LISTEN:
    nrf24_set_mode(RX_MODE);
    break;

  default:
    break;
  }
}

void ble_hop_channel()
{
  ble_instance.current_freq_index++;
  if (ble_instance.current_freq_index >= sizeof(channel))
    ble_instance.current_freq_index = 0;
  nrf24_set_channel(frequency[ble_instance.current_freq_index]);
}

static void add_adv_data(uint8_t data_size, uint8_t type, uint8_t *data)
{
  adv_data_t *ptr = (adv_data_t *)(ble_instance.adv_pdu_buffer.payload + ble_instance.adv_pl_size);
  ptr->length = data_size + 1;
  ptr->type = type;
  for (uint8_t i = 0; i < data_size; i++)
    ptr->data[i] = data[i];

  ble_instance.adv_pl_size += data_size + 2;
}

static void prepare_adv_pdu()
{
  ble_instance.adv_pl_size = 0;
  // add adv flasgs
  add_adv_data(1, 0x01, &ble_instance.adv_config_data->flags);

  // add complete local name
  add_adv_data(strlen(ble_instance.adv_config_data->adv_name), 0x09, (uint8_t *)ble_instance.adv_config_data->adv_name);

  // add service data
  uint8_t *service_data = (uint8_t *)ble_instance.adv_config_data->ble_service_data;
  pdu_service_data_ptr = ble_instance.adv_pdu_buffer.payload + ble_instance.adv_pl_size + 4; // sizeof UUID=2, advance 2 bytes forward to point to service data
  add_adv_data(SERVICE_DATA_SIZE, 0x16, service_data);

  // Add MAC address
  memcpy(ble_instance.adv_pdu_buffer.mac_address, mac_address, sizeof(mac_address));

  ble_instance.adv_pl_size += 6;

  ble_instance.adv_pdu_buffer.header = 0x22;
  ble_instance.adv_pdu_buffer.payload_length = ble_instance.adv_pl_size;
}

static void whiten_data(uint8_t *data, uint8_t length)
{
  uint8_t lfsr = channel[ble_instance.current_freq_index];
  lfsr |= 0x40;

  while (length--)
  {
    uint8_t res = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
      res |= (lfsr & 0x01) << i;

      if (lfsr & 0x01)
      {
        lfsr >>= 1;
        lfsr ^= 0x44;
      }
      else
        lfsr >>= 1;
    }

    *(data++) ^= res;
  }
}

static void compute_crc(uint8_t *data, uint8_t *crc_buf, uint8_t len)
{
  uint32_t crc = 0xAAAAAA;

  while (len--)
  {
    uint8_t byte = *(data++);
    for (uint8_t i = 0; i < 8; i++)
    {
      if ((byte & 0x01) ^ (crc & 0x01))
      {

        crc >>= 1;
        crc ^= 0xDA6000;
      }
      else
        crc >>= 1;
      byte >>= 1;
    }
  }
  uint8_t *ptr = (uint8_t *)&crc;
  for (uint8_t i = 0; i < 3; i++)
    crc_buf[i] = ptr[i];
}

static void update_adv_pdu()
{

  *pdu_service_data_ptr = ble_instance.adv_config_data->ble_service_data->data; // update service data;

  compute_crc((uint8_t *)&ble_instance.adv_pdu_buffer, ble_instance.adv_pdu_buffer.payload + ble_instance.adv_pl_size - 6, ble_instance.adv_pl_size + 2);

  whiten_data((uint8_t *)&ble_instance.adv_pdu_buffer, ble_instance.adv_pl_size + 5); // Header=1 byte, payload_length=1 byte, crc= 3 byte -> total=5 bytes to add to pl_size

  reverse_bit_order((uint8_t *)&ble_instance.adv_pdu_buffer, ble_instance.adv_pl_size + 5);
}

bool ble_advertise()
{
  if (ble_instance.ble_mode == BLE_MODE_ADVERTISE)
  {
    update_adv_pdu();

    if (ble_instance.adv_pl_size > 27)
    {
      printk("Oversized payload!\n");
      return false;
    }
    return nrf24_tx((uint8_t *)&ble_instance.adv_pdu_buffer, ble_instance.adv_pl_size + 5);
  }
  else
  {
    printk("Device not in advertising mode!\n");
    return false;
  }
}

void ble_adv_listen()
{

  if (!(ble_instance.ble_mode == BLE_MODE_LISTEN))
  {
    printk("Not in RX mode\n");
    return;
  }
  uint8_t name_len = 0;
  uint8_t payload_size = 0;
  uint8_t pipe;
  if (nrf24_available(&pipe))
  {
    printk("BLE device available\n");
    memset(&ble_instance.scan_pdu_buffer, 0, 32);
    nrf24_rx((uint8_t *)&ble_instance.scan_pdu_buffer, 32);
    reverse_bit_order((uint8_t *)&ble_instance.scan_pdu_buffer, 32);
    whiten_data((uint8_t *)&ble_instance.scan_pdu_buffer, 32);
    payload_size = ble_instance.scan_pdu_buffer.payload_length - 6; // exclude mac address length for payload

    if (payload_size > 21)
    {
      printk("ERROR:oversized payload\n");
      return;
    }

    uint8_t received_crc[3];
    for (uint8_t i = 0; i < 3; i++)
      received_crc[i] = ble_instance.scan_pdu_buffer.payload[payload_size + i];
    uint8_t crc[3];
    compute_crc((uint8_t *)&ble_instance.scan_pdu_buffer, crc, payload_size + 8);

    for (uint8_t i = 0; i < 3; i++)
    {
      if (crc[i] != received_crc[i])
      {
        printk("ERROR:CRC failed\n");
        return;
      }
    }
    printk("CRC OK!\n");

    printk("MAC address:->");
    for (uint8_t i = 0; i < 6; i++)
    {
      printk("%x", ble_instance.scan_pdu_buffer.mac_address[5 - i]);
      if (i != 5)
        printk(":");
    }
    printk("\n");

    uint8_t index = 0;
    while (index < payload_size)
    {
      adv_data_t *ptr = (adv_data_t *)(ble_instance.scan_pdu_buffer.payload + index);
      if (ptr->type == 0x09)
      {
        char dev_name[] = "";
        name_len = ptr->length - 1;
        memcpy((uint8_t *)dev_name, ptr->data, name_len);
        dev_name[name_len] = '\0';
        printk("Device name:-> %s", dev_name);
        /*
        for (uint8_t i = 0; i < 4; i++)
        {
          if (get_device_sum((uint8_t *)dev_name) == device_id[i])
          {
            match = true;
            printk("matched");
            break;
          }
        }

        if (match == false)
        {

          printk("not");
          device_id[device_index] = get_device_sum((uint8_t *)dev_name);
          device_index++;
          if (device_index > 4)
            device_index = 0;
          printString(("> "+String(dev_name)).c_str(), 0, y, 6, false);
          display();
          y += 16;
          printk("refresh");
        }

        refresh = refresh + 1;
        if (refresh == 15)
        {
          Serial.print("refresh");
          refresh = 0;
          clearDisplay();
          printString("<BLE Scanning>", 10, 0, 6, true);
          display();
          memset(device_id, 0, sizeof(device_id));
          y = 16;
          device_index = 0;
        }
        match = false;
      */
        memset(dev_name, 0, name_len);
        printk("\n");

        return;
      }
      index += ptr->length + 1;
    }
    printk("Name not available\n");
  }
}

bool ble_begin(ble_adv_config_t *adv_config_data)
{
  ble_instance.current_freq_index = 0;
  ble_instance.adv_pl_size = 0;

  if (!nrf24_init())
    return false;
  nrf24_set_address_width(4);
  nrf24_set_auto_ack(false);
  nrf24_set_arc_count(0);
  nrf24_set_retransmit_delay(0);
  nrf24_set_tx_power(TX_POWER_MAX);
  nrf24_set_tx_address(access_address);
  nrf24_set_rx_address(0, access_address);
  nrf24_disable_crc();
  nrf24_set_data_rate(DR_1MBPS);
  nrf24_set_channel(frequency[ble_instance.current_freq_index]);
  nrf24_power_up();

  if (adv_config_data != NULL) // if adv_config_data is NULL then it is in Scanning mode;
    ble_instance.adv_config_data = adv_config_data;
  prepare_adv_pdu();
  return true;
}
