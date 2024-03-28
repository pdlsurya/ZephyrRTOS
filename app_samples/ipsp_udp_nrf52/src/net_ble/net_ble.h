#ifndef _NET_BLE_H
#define _NET_BLE_H


void net_ble_init();

void net_udp_send(char *msg, int len, struct sockaddr *destination_addr);



extern const struct device *sh1106;

#endif