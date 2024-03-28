/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/udp.h>
#include <zephyr/console/console.h>
#include <net_ble.h>
#include <zephyr/net/dns_resolve.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <ntp_clock.h>
#include <sh1106_gfx.h>
#include <nordic_logo.h>

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec adv_led = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);
static const struct gpio_dt_spec conn_led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

const struct device *sh1106 = DEVICE_DT_GET(DT_NODELABEL(sh1106));

/* Custom Service Variables */
#define BT_UUID_CUSTOM_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_CUSTOM_SERVICE BT_UUID_DECLARE_128(BT_UUID_CUSTOM_SERVICE_VAL)

#define BT_UUID_CHRC1 BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2))

#define BT_UUID_CHRC2 BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1))

struct in6_addr dest_addr6 = {{{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0xef, 0xac, 0xb0, 0xff, 0xfe, 0x2f, 0xbd, 0xef}}};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_IPSS_VAL))};

static char write_msg[23] = "";
static char udp_msg[] = "Hello from 6LoWPAN node-2";
static uint8_t write_cnt;
static void start_scan(void);

static struct bt_conn *default_conn;
static struct bt_conn *connections[CONFIG_BT_MAX_CONN];
static uint8_t connection_cnt = 0;

static struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_write_params write_params;
static struct bt_gatt_read_params read_params;
static struct bt_gatt_exchange_params mtu_exchange_params;
bool mtu_exchange_done = false;

#define SCAN_INTERVAL 160	 /* 1000 ms */
#define SCAN_WINDOW 0x0030	 /* 30 ms */
#define INIT_INTERVAL 0x0020 /* 10 ms */
#define INIT_WINDOW 0x0020	 /* 10 ms */
#define CONN_INTERVAL 40	 /* 50 ms */
#define CONN_LATENCY 0
#define CONN_TIMEOUT 50

uint16_t chrc2_value_handle = 18;
uint16_t chrc1_value_handle = 20;
uint16_t chrc1_ccc_handle = 21;

uint8_t seconds;

#define DNS_TIMEOUT (2 * MSEC_PER_SEC)

void set_conn_led()
{
	gpio_pin_set_dt(&conn_led, 1);
	gpio_pin_set_dt(&adv_led, 0);
}

void set_adv_led()
{
	gpio_pin_set_dt(&adv_led, 1);
	gpio_pin_set_dt(&conn_led, 0);
}

static uint8_t notify_func(struct bt_conn *conn,
						   struct bt_gatt_subscribe_params *params,
						   const void *data, uint16_t length)
{
	if (!data)
	{
		printk("[UNSUBSCRIBED]\n");
		return BT_GATT_ITER_STOP;
	}
	((char *)data)[length] = 0;
	printk("[NOTIFICATION]length :%u <---> data: %s \n", length, ((char *)data));
	net_udp_send((char *)data, length, NULL);

	return BT_GATT_ITER_CONTINUE;
}

static void write_response(struct bt_conn *conn, uint8_t err,
						   struct bt_gatt_write_params *params)
{
	if (err)

		printk("Write error!\n");

	else
		printk("Write success!\n");
}

static uint8_t read_response(struct bt_conn *conn, uint8_t err,
							 struct bt_gatt_read_params *params,
							 const void *data, uint16_t length)
{
	if (data != NULL)
	{
		((char *)data)[length] = 0;
		printk("Read data:-> %s\n", (char *)data);
		return BT_GATT_ITER_CONTINUE;
	}
	return BT_GATT_ITER_STOP;
};

static uint8_t discover_func(struct bt_conn *conn,
							 const struct bt_gatt_attr *attr,
							 struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr)
	{
		printk("Discover complete\n");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_CUSTOM_SERVICE))
	{

		memcpy(&uuid, BT_UUID_CHRC2, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err)
		{
			printk("Characterstic Discover failed (err %d)\n", err);
		}
	}
	else if (!bt_uuid_cmp(discover_params.uuid,
						  BT_UUID_CHRC2))
	{

		write_params.handle = bt_gatt_attr_value_handle(attr);
		write_params.func = write_response;
		write_params.data = write_msg;
		write_params.offset = 0;

		memcpy(&uuid, BT_UUID_CHRC1, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err)
		{
			printk("Characterstics Discover failed (err %d)\n", err);
		}
	}
	else if (!bt_uuid_cmp(discover_params.uuid,
						  BT_UUID_CHRC1))
	{
		memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;

		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		read_params.handle_count = 1;
		read_params.func = read_response;
		read_params.single.handle = bt_gatt_attr_value_handle(attr);
		read_params.single.offset = 0;

		err = bt_gatt_discover(conn, &discover_params);
		if (err)
		{
			printk("Descripter Discover failed (err %d)\n", err);
		}
	}
	else
	{
		subscribe_params.notify = notify_func;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY)
		{
			printk("Subscribe failed (err %d)\n", err);
		}
		else
		{
			printk("[SUBSCRIBED]\n");
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
	int i;

	if (default_conn)
		return false;

	// printk("[AD]: %u data_len %u\n", data->type, data->data_len);

	switch (data->type)
	{
	case BT_DATA_UUID128_SOME:
	case BT_DATA_UUID128_ALL:
		if (data->data_len % sizeof(uint16_t) != 0U)
		{
			printk("AD malformed\n");
			return true;
		}

		for (i = 0; i < data->data_len; i += sizeof(uint16_t))
		{
			struct bt_conn_le_create_param create_param = {
				.options = BT_CONN_LE_OPT_NONE,
				.interval = INIT_INTERVAL,
				.window = INIT_WINDOW,
				.interval_coded = 0,
				.window_coded = 0,
				.timeout = 0,
			};
			struct bt_le_conn_param conn_param = {
				.interval_min = CONN_INTERVAL,
				.interval_max = CONN_INTERVAL,
				.latency = CONN_LATENCY,
				.timeout = CONN_TIMEOUT,
			};

			struct bt_uuid *uuid;
			struct bt_uuid_128 uuid128;
			int err;

			memcpy(&uuid128.val[0], &data->data[i], 16);
			uuid128.uuid.type = BT_UUID_TYPE_128;

			uuid = (struct bt_uuid *)&uuid128;

			if (bt_uuid_cmp(uuid, BT_UUID_CUSTOM_SERVICE))
			{
				printk("No match\n");
				continue;
			}

			err = bt_le_scan_stop();
			if (err)
			{
				printk("Stop LE scan failed (err %d)\n", err);
				continue;
			}

			err = bt_conn_le_create(addr, &create_param,
									&conn_param, &default_conn);
			if (err)
			{
				printk("Create conn failed (err %d)\n", err);
				start_scan();
			}

			return false;
		}
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
						 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	// printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",dev, type, ad->len, rssi);

	/* We're only interested in connectable events */
	if (type == BT_GAP_ADV_TYPE_ADV_IND ||
		type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND)
	{
		bt_data_parse(ad, eir_found, (void *)addr);
	}
}

static void start_scan(void)
{
	int err;

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime. */
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = SCAN_INTERVAL,
		.window = SCAN_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err)
	{
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
							struct bt_gatt_exchange_params *params)
{
	printk("%s: MTU exchange %s (%u)\n", __func__,
		   err == 0U ? "successful" : "failed",
		   bt_gatt_get_mtu(conn));
}

static int mtu_exchange(struct bt_conn *conn)
{
	int err;

	printk("%s: Current MTU = %u\n", __func__, bt_gatt_get_mtu(conn));

	mtu_exchange_params.func = mtu_exchange_cb;

	printk("%s: Exchange MTU...\n", __func__);
	err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (err)
	{
		printk("%s: MTU exchange failed (err %d)", __func__, err);
	}

	return err;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;
	struct bt_conn_info conn_info;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	err = bt_conn_get_info(conn, &conn_info);
	if (err)
	{
		printk("Failed to get connection info (%d).\n", err);
		return;
	}

	if (conn_info.role == BT_CONN_ROLE_CENTRAL)
	{

		if (conn_err)
		{
			printk("Failed to connect to %s (%u)\n", addr, conn_err);

			connections[bt_conn_index(default_conn)] = NULL;
			bt_conn_unref(default_conn);
			default_conn = NULL;

			start_scan();
			return;
		}

		default_conn = NULL;

		printk("Connected: %s--Role=Central\n", addr);

		(void)mtu_exchange(conn);

		connections[bt_conn_index(conn)] = conn;

		connection_cnt++;

		// Start scanning for other devices
		if (connection_cnt < CONFIG_BT_MAX_CONN - 1)
			start_scan();

		memcpy(&uuid, BT_UUID_CUSTOM_SERVICE, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(conn, &discover_params);
		if (err)
		{
			printk("Service discover failed(err %d)\n", err);
			return;
		}

		/*
			subscribe_params.value_handle = chrc1_value_handle;
			subscribe_params.notify = notify_func;
			subscribe_params.value = BT_GATT_CCC_NOTIFY;
			subscribe_params.ccc_handle = chrc1_ccc_handle;

			err = bt_gatt_subscribe(conn, &subscribe_params);
			if (err && err != -EALREADY)
			{
				printk("Subscribe failed (err %d)\n", err);
			}
			else
			{
				printk("[SUBSCRIBED]\n");
			}
			*/
	}
	else
	{
		set_conn_led();
		printk("Connected to BLE 6LoWPAN Border Router:-> %s\n", addr);

		bt_le_adv_stop();
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info conn_info;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	int err = bt_conn_get_info(conn, &conn_info);
	if (err)
	{
		printk("Failed to get connection info (%d).\n", err);
		return;
	}

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	default_conn = NULL;
	if (conn_info.role == BT_CONN_ROLE_CENTRAL)
	{
		bt_conn_unref(conn);
		connections[bt_conn_index(conn)] = NULL;
		connection_cnt--;
		start_scan();
	}

	else
	{
		set_adv_led();
		err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
		if (err)
		{
			printk("Advertising failed to start (err %d)\n", err);
			return;
		}
	}
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("%s: int (0x%04x, 0x%04x) lat %u to %u\n", __func__,
		   param->interval_min, param->interval_max, param->latency,
		   param->timeout);

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
							 uint16_t latency, uint16_t timeout)
{
	printk("%s: int 0x%04x lat %u to %u\n", __func__, interval,
		   latency, timeout);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated};

int leds_init()
{
	int err;

	if (!gpio_is_ready_dt(&adv_led))
		return 0;
	err = gpio_pin_configure_dt(&adv_led, GPIO_OUTPUT_ACTIVE);
	if (err < 0)
		return 0;

	if (!gpio_is_ready_dt(&conn_led))
		return 0;
	err = gpio_pin_configure_dt(&conn_led, GPIO_OUTPUT_INACTIVE);
	if (err < 0)
		return 0;
	return 0;
}

int main(void)
{

	net_ble_init();
	ntp_clock_init();

	/*
	err = bt_enable(NULL);

	if (err)
	{
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}
	printk("Bluetooth initialized\n");

   */

	// start_scan();
	/*
	read_params.handle_count = 1;
	read_params.func = read_response;
	read_params.single.handle = chrc1_value_handle;
	read_params.single.offset = 0;

	write_params.handle = chrc2_value_handle;
	write_params.func = write_response;
	write_params.data = write_msg;
	write_params.offset = 0;
*/
	while (1)
	{
		/*
		sprintf(write_msg, "Hi from Ble-dev->%d", write_cnt++);
		write_params.length = strlen(write_msg);

		for (uint8_t i = 0; i < CONFIG_BT_MAX_CONN; i++)
		{
			if (connections[i] != NULL)
			{
				if (bt_gatt_write(connections[i], &write_params))
					printk("Write queue failed\n");

				if (bt_gatt_read(connections[i], &read_params))
					printk("Read failed\n");
			}
		}
		*/
		k_msleep(500);
		ntp_update_time();
	}
	return 0;
}

void myThread1(void *, void *, void *)
{
/*
	if (!device_is_ready(sh1106))
	{
		printk("Failed to initialize sh1106 display\n");
		while (1)
			;
	}

	oled_displayOn(sh1106);
	oled_clearDisplay(sh1106);
	oled_printString(sh1106, "Powered by Zephyr OS.", 0, 0, 6, false);

	oled_display(sh1106);
	k_msleep(2000);
	oled_clearDisplay(sh1106);
	oled_displayBmp(sh1106, logo);
	oled_display(sh1106);
	oled_clearDisplay(sh1106);
*/
	struct sockaddr dest_sock_addr;
	net_sin6(&dest_sock_addr)->sin6_family = AF_INET6;
	net_sin6(&dest_sock_addr)->sin6_port = htons(4242);
	net_ipv6_addr_copy_raw(net_sin6(&dest_sock_addr)->sin6_addr.s6_addr,
						   dest_addr6.s6_addr);

	while (1)
	{
		net_udp_send(udp_msg, strlen(udp_msg), &dest_sock_addr);
		k_msleep(5000);
	}
}

K_THREAD_DEFINE(oled_tid, 1024,
				myThread1, NULL, NULL, NULL,
				4, 0, 0);
SYS_INIT(leds_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);