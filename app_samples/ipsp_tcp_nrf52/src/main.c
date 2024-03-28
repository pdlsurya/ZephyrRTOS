/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ipsp);

/* Preventing log module registration in net_core.h */
#define NET_LOG_ENABLED	0

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include<zephyr/drivers/gpio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/display/sh1106_gfx.h>
#include <zephyr/display/nordic_logo.h>
#include <zephyr/console/console.h>

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

const struct device *sh1106 = DEVICE_DT_GET(DT_NODELABEL(sh1106));

/* Define my IP address where to expect messages */
#define MY_IP6ADDR { { { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, \
			 0, 0, 0, 0, 0, 0, 0, 0x1 } } }
#define MY_PREFIX_LEN 64

static struct in6_addr in6addr_my = MY_IP6ADDR;

#define MY_PORT 4242

#define STACKSIZE 2000
K_THREAD_STACK_DEFINE(thread_stack, STACKSIZE);
static struct k_thread thread_data;

static uint8_t buf_tx[NET_IPV6_MTU];

#define MAX_DBG_PRINT 64

NET_PKT_TX_SLAB_DEFINE(echo_tx_tcp, 15);
NET_PKT_DATA_POOL_DEFINE(echo_data_tcp, 30);

static struct k_mem_slab *tx_tcp_pool(void)
{
	return &echo_tx_tcp;
}

static struct net_buf_pool *data_tcp_pool(void)
{
	return &echo_data_tcp;
}

static struct k_sem quit_lock;

static inline void quit(void)
{
	k_sem_give(&quit_lock);
}

static inline void init_app(void)
{
	LOG_INF("Run IPSP sample");

	k_sem_init(&quit_lock, 0, K_SEM_MAX_LIMIT);
    /*
	if (net_addr_pton(AF_INET6,
			  CONFIG_NET_CONFIG_MY_IPV6_ADDR,
			  &in6addr_my) < 0) {
		LOG_ERR("Invalid IPv6 address %s",
			CONFIG_NET_CONFIG_MY_IPV6_ADDR);
	}
    */
	do {
		struct net_if_addr *ifaddr;

		ifaddr = net_if_ipv6_addr_add(net_if_get_default(),
					      &in6addr_my,NET_ADDR_OVERRIDABLE, 0);
	} while (0);
}

static inline bool get_context(struct net_context **tcp_recv6)
{
	int ret;
	struct sockaddr_in6 my_addr6 = { 0 };

	my_addr6.sin6_family = AF_INET6;
	my_addr6.sin6_port = htons(MY_PORT);

	ret = net_context_get(AF_INET6, SOCK_STREAM, IPPROTO_TCP, tcp_recv6);
	if (ret < 0) {
		LOG_ERR("Cannot get network context for IPv6 TCP (%d)", ret);
		return false;
	}

	net_context_setup_pools(*tcp_recv6, tx_tcp_pool, data_tcp_pool);

	ret = net_context_bind(*tcp_recv6, (struct sockaddr *)&my_addr6,
			       sizeof(struct sockaddr_in6));
	if (ret < 0) {
		LOG_ERR("Cannot bind IPv6 TCP port %d (%d)",
			ntohs(my_addr6.sin6_port), ret);
		return false;
	}

	ret = net_context_listen(*tcp_recv6, 0);
	if (ret < 0) {
		LOG_ERR("Cannot listen IPv6 TCP (%d)", ret);
		return false;
	}

	return true;
}

static int build_reply(const char *name,
		       struct net_pkt *pkt,
		       uint8_t *buf)
{
	int reply_len = net_pkt_remaining_data(pkt);
	int ret;

	LOG_DBG("%s received %d bytes", name, reply_len);

	ret = net_pkt_read(pkt, buf, reply_len);
	LOG_INF("%s\n",buf);
	if (ret < 0) {
		LOG_ERR("cannot read packet: %d", ret);
		return ret;
	}

	LOG_DBG("sending %d bytes", reply_len);

	return reply_len;
}

static inline void pkt_sent(struct net_context *context,
			    int status,
			    void *user_data)
{
	if (status >= 0) {
		LOG_DBG("Sent %d bytes", status);
	}
}

static inline void set_dst_addr(sa_family_t family,
				struct net_pkt *pkt,
				struct net_ipv6_hdr *ipv6_hdr,
				struct net_udp_hdr *udp_hdr,
				struct sockaddr *dst_addr)
{
	net_ipv6_addr_copy_raw((uint8_t *)&net_sin6(dst_addr)->sin6_addr,
			       ipv6_hdr->src);
	net_sin6(dst_addr)->sin6_family = AF_INET6;
	net_sin6(dst_addr)->sin6_port = udp_hdr->src_port;
}


static void tcp_received(struct net_context *context,
			 struct net_pkt *pkt,
			 union net_ip_header *ip_hdr,
			 union net_proto_header *proto_hdr,
			 int status,
			 void *user_data)
{
	static char dbg[MAX_DBG_PRINT + 1];
	sa_family_t family;
	int ret, len;

	if (!pkt) {
		/* EOF condition */
		return;
	}

	family = net_pkt_family(pkt);
	len = net_pkt_remaining_data(pkt);

	snprintf(dbg, MAX_DBG_PRINT, "TCP IPv%c",
		 family == AF_INET6 ? '6' : '4');

	ret = build_reply(dbg, pkt, buf_tx);
	if (ret < 0) {
		LOG_ERR("Cannot send data to peer (%d)", ret);
		return;
	}

	(void)net_context_update_recv_wnd(context, len);
	net_pkt_unref(pkt);

	ret = net_context_send(context, buf_tx, ret, pkt_sent,
			       K_NO_WAIT, NULL);
	if (ret < 0) {
		LOG_ERR("Cannot send data to peer (%d)", ret);
		quit();
	}
	else{
		LOG_INF("Tcp sent");
	}
}

static void tcp_accepted(struct net_context *context,
			 struct sockaddr *addr,
			 socklen_t addrlen,
			 int error,
			 void *user_data)
{
	int ret;

	LOG_DBG("Accept called, context %p error %d", context, error);

	net_context_set_accepting(context, false);

	ret = net_context_recv(context, tcp_received, K_NO_WAIT, NULL);
	if (ret < 0) {
		LOG_ERR("Cannot receive TCP packet (family %d)",
			net_context_get_family(context));
	}
	else{
		LOG_INF("Tcp accepted");
	}
	
}

static void setup_tcp_accept(struct net_context *tcp_recv6)
{
	int ret;

	ret = net_context_accept(tcp_recv6, tcp_accepted, K_NO_WAIT, NULL);
	if (ret < 0) {
		LOG_ERR("Cannot receive IPv6 TCP packets (%d)", ret);
	}
}

static void listen(void)
{
	struct net_context *tcp_recv6 = { 0 };

	if (!get_context(&tcp_recv6)) {
		LOG_ERR("Cannot get network contexts");
		return;
	}

	LOG_INF("Starting to wait");

	setup_tcp_accept(tcp_recv6);
	
	k_sem_take(&quit_lock, K_FOREVER);

	LOG_INF("Stopping...");

	net_context_put(tcp_recv6);
}

int main(void)
{
	init_app();

	k_thread_create(&thread_data, thread_stack, STACKSIZE,
			(k_thread_entry_t)listen,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	return 0;
}


void myThread1(void *, void *, void *)
{

	if (!device_is_ready(sh1106))
	{
		printk("Failed to initialize sh1106 display\n");
		while (1)
			;
	}

	//console_getline_init();

	oled_displayOn(sh1106);
	oled_clearDisplay(sh1106);
	oled_printString(sh1106, "Powered by Zephyr OS.", 0, 0, 6, false);

	oled_display(sh1106);
	k_msleep(2000);
	oled_clearDisplay(sh1106);
	oled_displayBmp(sh1106, logo);
	oled_display(sh1106);
	oled_clearDisplay(sh1106);
    uint8_t cnt=0;
	char str[]="";
	while (1)
	{
		sprintf(str,"Log count=%d",cnt++);
		//s = console_getline();
		oled_printLog(sh1106, str);
		oled_display(sh1106);
        k_msleep(500);
	}
}

int myThread0(void *, void *, void *)
{
	int ret;
	if (!gpio_is_ready_dt(&led0))
		return 0;
	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0)
		return 0;

	while (1)
	{

		ret = gpio_pin_toggle_dt(&led0);
		if (ret < 0)
			return 0;
		k_msleep(500);
		// k_sem_give(&sem);
	}
	return 0;
}
K_THREAD_DEFINE(my_tid, 500,
				myThread0, NULL, NULL, NULL,
				5, 0, 0);

K_THREAD_DEFINE(oled_tid, 1024,
				myThread1, NULL, NULL, NULL,
				4, 0, 0);