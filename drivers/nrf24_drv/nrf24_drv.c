/**
 * @file nrf24_driver.cpp
 * @author Surya Poudel (poudel.surya2011@gmail.com)
 * @brief Driver for nRF24 2.4GHz modules on SPI bus
 * @version 0.1
 * @date 2023-07-06
 *
 * @copyright Copyright(c) 2023, Surya Poudel
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <nrf24_drv.h>
#include <zephyr/sys/printk.h>

static const struct spi_dt_spec nrf24_spi_spec = SPI_DT_SPEC_GET(DT_NODELABEL(nrf24), (SPI_WORD_SET(8) | SPI_OP_MODE_MASTER), 0);

static const struct gpio_dt_spec ce_pin_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(nrf24), ce_gpios);

static nrf24_register_t nrf24_reg;

static int nrf24_spi_trasceive(uint8_t *p_txd, uint8_t *p_rxd, uint8_t len)
{
	struct spi_buf tx_buf =
		{
			.buf = p_txd,
			.len = len};
	struct spi_buf_set tx_bufs =
		{
			.buffers = &tx_buf,
			.count = 1};

	if (p_rxd != NULL)
	{

		struct spi_buf rx_buf =
			{
				.buf = p_rxd,
				.len = len};
		struct spi_buf_set rx_bufs =
			{
				.buffers = &rx_buf,
				.count = 1};
		return spi_transceive_dt(&nrf24_spi_spec, &tx_bufs, &rx_bufs);
	}
	else
		return spi_write_dt(&nrf24_spi_spec, &tx_bufs);
}

static void write_register(uint8_t reg_address, const uint8_t *reg_value, uint8_t len)
{
	uint8_t wr_reg[7] = {0};
	wr_reg[0] = (reg_address | W_REGISTER);

	for (uint8_t i = 1; i <= len; i++)
		wr_reg[i] = reg_value[i - 1];
	if (nrf24_spi_trasceive(wr_reg, NULL, len + 1) != 0)
		printk("register write failed!\n");
}

static void read_register(uint8_t reg_address, uint8_t *reg_value)
{
	uint8_t rd_reg[2] = {0};
	rd_reg[0] = (reg_address | R_REGISTER);

	if (nrf24_spi_trasceive(rd_reg, rd_reg, 2) != 0)
		printk("register read failed!\n");

	*reg_value = rd_reg[1];
}

uint8_t read_status()
{
	uint8_t radio_status;
	uint8_t tx_byte = NOPP;

	if (nrf24_spi_trasceive(&tx_byte, &radio_status, 1) != 0)
		printk("read status failed\n");

	return radio_status;
}

void nrf24_clear_irq_flags(uint8_t flags)
{
	nrf24_reg.REG_STATUS |= flags;
	write_register(RADIO_STATUS, &nrf24_reg.REG_STATUS, 1);
}

bool nrf24_tx(uint8_t *buffer, uint8_t length)
{

	uint8_t tx_buffer[33] = {0};
	tx_buffer[0] = W_TX_PAYLOAD;
	for (uint8_t i = 1; i <= length; i++)
		tx_buffer[i] = buffer[i - 1];

	if (nrf24_spi_trasceive(tx_buffer, NULL, 33) != 0)
		printk("Data write failed!\n");

	gpio_pin_set_dt(&ce_pin_spec, 1);
	k_sleep(K_USEC(10));
	gpio_pin_set_dt(&ce_pin_spec, 0);

	while (!((read_status() >> TX_DS) & 0x01))
	{
		if ((read_status() >> MAX_RT) & 0x01)
		{
			nrf24_clear_irq_flags((BIT(TX_DS) | BIT(RX_DR) | BIT(MAX_RT)));
			nrf24_flush_tx();
			nrf24_flush_rx();
			printk("retries over\n");
			return false;
		}
	}

	nrf24_clear_irq_flags((BIT(TX_DS) | BIT(RX_DR) | BIT(MAX_RT)));
	return true;
}

void nrf24_tx_no_ack(uint8_t *buffer, uint8_t length)
{

	uint8_t tx_buffer[33] = {0};
	tx_buffer[0] = W_TX_PAYLOAD_NO_ACK;
	for (uint8_t i = 1; i <= length; i++)
		tx_buffer[i] = buffer[i - 1];

	nrf24_spi_trasceive(tx_buffer, NULL, 33);

	gpio_pin_set_dt(&ce_pin_spec, 1);
	k_sleep(K_USEC(10));
	gpio_pin_set_dt(&ce_pin_spec, 0);

	while (!((read_status() >> TX_DS) & 0x01))
		;
	nrf24_clear_irq_flags((BIT(TX_DS) | BIT(RX_DR) | BIT(MAX_RT)));
}

void nrf24_set_arc_count(uint8_t count)
{
	if (count > 15)
		count = 15;
	nrf24_reg.REG_SETUP_RETR = count;
	write_register(SETUP_RETR, &nrf24_reg.REG_SETUP_RETR, 1);
}

void nrf24_set_retransmit_delay(uint8_t level)
{

	nrf24_reg.REG_SETUP_RETR |= level << 4;
	write_register(SETUP_RETR, &nrf24_reg.REG_SETUP_RETR, 1);
}

bool nrf24_available(uint8_t *pipe)
{
	uint8_t rx_pipe;

	if ((read_status() >> RX_DR) & 0x01)
	{

		rx_pipe = (read_status() >> 1) & 0x07;
		if (rx_pipe > 5)
			return false;
		else
		{
			if (pipe)
				*pipe = rx_pipe;
			return true;
		}
	}
	else

		return false;
}

void nrf24_rx(uint8_t *buffer, uint8_t length)
{
	uint8_t rx_buffer[33] = {0};
	rx_buffer[0] = R_RX_PAYLOAD;
	nrf24_spi_trasceive(rx_buffer, rx_buffer, 33);

	for (uint8_t i = 0; i < length; i++)
		buffer[i] = rx_buffer[i + 1];

	nrf24_clear_irq_flags(BIT(RX_DR));
	nrf24_flush_rx();
	nrf24_flush_tx();
}

void nrf24_set_mode(radio_mode_t mode)
{
	switch (mode)
	{
	case TX_MODE:
		gpio_pin_set_dt(&ce_pin_spec, 0);
		nrf24_reg.REG_CONFIG &= 0xFE;
		break;
	case RX_MODE:
		gpio_pin_set_dt(&ce_pin_spec, 1);
		nrf24_reg.REG_CONFIG |= 0x01;
		break;
	default:
		break;
	}

	write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
}

void nrf24_power_up()
{
	nrf24_reg.REG_CONFIG |= 0x01 << 1;
	write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
	k_sleep(K_USEC(750));
}

void nrf24_config_irq_pin(uint8_t mask)
{

	nrf24_reg.REG_CONFIG |= mask;

	write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
}

void nrf24_enable_crc()
{
	nrf24_reg.REG_CONFIG |= 0x01 << 3;
	write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
}

void nrf24_disable_crc()
{
	nrf24_reg.REG_CONFIG &= 0xF7;
	write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
}

void nrf24_set_address_width(uint8_t width)
{
	if (width == 3)
		nrf24_reg.REG_SETUP_AW |= 0X01;
	else if (width == 4)
		nrf24_reg.REG_SETUP_AW |= 0X02;
	else if (width == 5)
		nrf24_reg.REG_SETUP_AW |= 0X03;
	else
		nrf24_reg.REG_SETUP_AW |= 0X03;

	write_register(SETUP_AW, &nrf24_reg.REG_SETUP_AW, 1);
}

void nrf24_set_channel(uint8_t channel)
{

	nrf24_reg.REG_RF_CH = channel;
	if (channel > 125)
		nrf24_reg.REG_RF_CH = 125;

	write_register(RF_CH, &nrf24_reg.REG_RF_CH, 1);
}

void nrf24_set_data_rate(nrf24_data_rate_t data_rate)
{
	switch (data_rate)
	{
	case DR_1MBPS:
		nrf24_reg.REG_RF_SETUP |= 0x00;
		break;

	case DR_2MBPS:
		nrf24_reg.REG_RF_SETUP |= 0x08;
		break;
	case DR_250KBPS:
		nrf24_reg.REG_RF_SETUP |= 0x20;
		break;
	default:
		break;
	}

	write_register(RF_SETUP, &nrf24_reg.REG_RF_SETUP, 1);
}

void nrf24_set_tx_power(nrf24_tx_power_t power_level)
{
	switch (power_level)
	{
	case TX_POWER_MAX:
		nrf24_reg.REG_RF_SETUP |= 0X06;
		break;

	case TX_POWER_MODERATE:
		nrf24_reg.REG_RF_SETUP |= 0x04;
		break;
	case TX_POWER_MIN:
		nrf24_reg.REG_RF_SETUP |= 0x00;
		break;
	default:
		break;
	}
	write_register(RF_SETUP, &nrf24_reg.REG_RF_SETUP, 1);
}

void nrf24_set_rx_address(uint8_t pipe, const uint8_t *address)
{
	nrf24_enable_rx_pipe(pipe);

	for (uint8_t i = 0; i < 5; i++)
	{
		nrf24_reg.REG_RX_ADDR_PX[i] = address[i];
	}
	if (pipe >= 0 && pipe <= 5)
	{
		if (pipe < 2)
			write_register(RX_ADDR_P0 + pipe, nrf24_reg.REG_RX_ADDR_PX, 5);
		else
			write_register(RX_ADDR_P0 + pipe, nrf24_reg.REG_RX_ADDR_PX, 1);
	}
}
void nrf24_set_tx_address(const uint8_t *address)
{

	nrf24_set_rx_address(0, address); // for receiving ack packets

	for (uint8_t i = 0; i < 5; i++)
		nrf24_reg.REG_TX_ADDR[i] = address[i];
	write_register(TX_ADDR, nrf24_reg.REG_TX_ADDR, 5);
}

void nrf24_enable_dynamic_payload_length()
{
	nrf24_reg.REG_FEATURE |= 0x04;
	write_register(FEATURE, &nrf24_reg.REG_FEATURE, 1);

	nrf24_reg.REG_DYNPD |= 0x3F;
	write_register(DYNPD, &nrf24_reg.REG_DYNPD, 1);
}

void nrf24_disable_dynamic_payload_length()
{
	nrf24_reg.REG_FEATURE &= 0xFB;
	write_register(FEATURE, &nrf24_reg.REG_FEATURE, 1);

	nrf24_reg.REG_DYNPD &= 0x00;
	write_register(DYNPD, &nrf24_reg.REG_DYNPD, 1);
}

void nrf24_set_crc_encoding(uint8_t byte_cnt)
{
	if (byte_cnt > 0 && byte_cnt <= 2)
	{
		nrf24_reg.REG_CONFIG |= (byte_cnt - 1) << 2;
		write_register(CONFIG, &nrf24_reg.REG_CONFIG, 1);
	}
	else
		return;
}

void nrf24_set_payload_size(uint8_t size)
{
	nrf24_reg.REG_RX_PW_PX = size;
	for (uint8_t i = 0; i < 6; i++)
	{
		write_register(RX_PW_P0 + i, &nrf24_reg.REG_RX_PW_PX, 1);
	}
}

void nrf24_set_auto_ack(bool set_value)
{
	if (set_value)

		nrf24_reg.REG_EN_AA |= 0x3F;
	else
		nrf24_reg.REG_EN_AA &= 0x00;

	write_register(EN_AA, &nrf24_reg.REG_EN_AA, 1);
}

void nrf24_enable_rx_pipe(uint8_t pipe)
{
	nrf24_reg.REG_EN_RXADDR |= 0x01 << pipe;
	write_register(EN_RXADDR, &nrf24_reg.REG_EN_RXADDR, 1);
}

void nrf24_flush_tx()
{
	uint8_t tx_byte = FLUSH_TX;
	nrf24_spi_trasceive(&tx_byte, NULL, 1);
}

void nrf24_flush_rx()
{
	uint8_t tx_byte = FLUSH_RX;
	nrf24_spi_trasceive(&tx_byte, NULL, 1);
}

uint8_t nrf24_get_rx_payload_size()
{
	uint8_t rx_pl_size;
	read_register(R_RX_PL_WID, &rx_pl_size);
	if (rx_pl_size > 32)
		nrf24_flush_rx();
	return rx_pl_size;
}

void nrf24_enable_dynamic_ack()
{
	nrf24_reg.REG_FEATURE |= 0x01;
	write_register(FEATURE, &nrf24_reg.REG_FEATURE, 1);
}

void nrf24_enable_ack_payload()
{
	nrf24_reg.REG_FEATURE |= 0x01 << 1;
	write_register(FEATURE, &nrf24_reg.REG_FEATURE, 1);
}

bool nrf24_init()
{
	// Configure CE pin
	if (!gpio_is_ready_dt(&ce_pin_spec))
	{
		printk("CE pin config failed\n");
		return false;
	}
	gpio_pin_configure_dt(&ce_pin_spec, GPIO_OUTPUT_INACTIVE);

	if (!spi_is_ready_dt(&nrf24_spi_spec))
	{
		printk("SPI bus config failed\n");
		return false;
	}

	memset(&nrf24_reg, 0, sizeof(nrf24_reg));

	k_sleep(K_MSEC(5));
	// Default setup
	nrf24_config_irq_pin(RX_IRQ_MASK);
	nrf24_disable_dynamic_payload_length();
	nrf24_enable_crc();
	nrf24_set_crc_encoding(2);
	nrf24_set_address_width(5);
	nrf24_set_payload_size(32);
	nrf24_set_arc_count(0);
	nrf24_set_auto_ack(false);
	nrf24_set_retransmit_delay(0);
	nrf24_set_data_rate(DR_1MBPS);
	nrf24_enable_rx_pipe(0);
	nrf24_enable_rx_pipe(1);
	nrf24_set_tx_power(TX_POWER_MAX);
	nrf24_flush_tx();
	nrf24_flush_rx();
	nrf24_clear_irq_flags((BIT(TX_DS) | BIT(RX_DR) | BIT(MAX_RT)));
	nrf24_power_up();

	return true;
}