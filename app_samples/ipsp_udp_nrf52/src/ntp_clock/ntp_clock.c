#include <zephyr/logging/log.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ntp_clock);

/* Preventing log module registration in net_core.h */
#define NET_LOG_ENABLED 0
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/udp.h>
#include <sh1106_gfx.h>
#include <ntp_clock.h>
#include <time.h>

#define NTP_SERVER                                                                                             \
    {                                                                                                          \
        {                                                                                                      \
            {                                                                                                  \
                0x24, 0x01, 0xff, 0x40, 0x00, 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05 \
            }                                                                                                  \
        }                                                                                                      \
    }
#define NTP_SERVER_PORT 123

#define NTP_TIMESTAMP_DELTA 2208988800ull

const char *weekdays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static struct in6_addr in6addr_ntp = NTP_SERVER;

ntp_packet_t ntp_packet = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const long nepal_offset = 20700; // UTC+5:45

static uint32_t updateInterval = 2048000; // in ms

static uint32_t lastUpdate;

static time_t epochTime;

bool update_received = false;
bool first_updated = false;

struct net_context *ntp_udp = {0};

#define MY_LOCAL_PORT 4369

#define NTP_THREAD_STACKSIZE 500
K_THREAD_STACK_DEFINE(ntp_thread_stack, NTP_THREAD_STACKSIZE);
static struct k_thread ntp_thread;

static struct k_sem ntp_quit_lock;

static inline void ntp_quit(void)
{
    k_sem_give(&ntp_quit_lock);
}

static inline bool ntp_clock_get_context(struct net_context **ntp_udp)
{
    int ret;
    struct sockaddr_in6 my_addr6 = {0};

    my_addr6.sin6_family = AF_INET6;
    my_addr6.sin6_port = htons(MY_LOCAL_PORT);

    ret = net_context_get(AF_INET6, SOCK_DGRAM, IPPROTO_UDP, ntp_udp);
    if (ret < 0)
    {
        LOG_ERR("Cannot get network context for IPv6 UDP (%d)", ret);
        return false;
    }

    ret = net_context_bind(*ntp_udp, (struct sockaddr *)&my_addr6,
                           sizeof(struct sockaddr_in6));
    if (ret < 0)
    {
        LOG_ERR("Cannot bind IPv6 UDP port %d (%d)",
                ntohs(my_addr6.sin6_port), ret);
        return false;
    }

    return true;
}

static void ntp_udp_callback(struct net_context *context,
                             struct net_pkt *pkt,
                             union net_ip_header *ip_hdr,
                             union net_proto_header *proto_hdr,
                             int status,
                             void *user_data)
{
    printk("Received from NTP Server:");
    for (uint8_t i = 0; i < 16; i++)
        printk("%x ", ip_hdr->ipv6->src[i]);
    printk("\n");

    int pkt_len = net_pkt_remaining_data(pkt);
    int ret;

    memset(&ntp_packet, 0, sizeof(ntp_packet_t));

    ret = net_pkt_read(pkt, (uint8_t *)&ntp_packet, pkt_len);

    update_received = true;
    if (!first_updated)
        first_updated = true;

    net_pkt_unref(pkt);

    lastUpdate = k_uptime_get_32();

    return;
}

static void ntp_setup_callback(struct net_context *ntp_udp)
{
    int ret;

    ret = net_context_recv(ntp_udp, ntp_udp_callback, K_NO_WAIT, NULL);
    if (ret < 0)
    {
        LOG_ERR("Cannot receive IPv6 UDP packets");
    }
}

static void ntp_request_server()
{
    printk("Polling NTP server\n");
    struct sockaddr sockaddr_ntp;
    memset((uint8_t *)&ntp_packet, 0, sizeof(ntp_packet_t));
    // Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.
    ntp_packet.li_vn_mode = 0b11100011;
    ntp_packet.poll = 11;
    net_sin6(&sockaddr_ntp)->sin6_family = AF_INET6;
    net_sin6(&sockaddr_ntp)->sin6_port = htons(NTP_SERVER_PORT);
    net_ipv6_addr_copy_raw((uint8_t *)&net_sin6(&sockaddr_ntp)->sin6_addr,
                           (uint8_t *)&in6addr_ntp);

    int ret = net_context_sendto(ntp_udp, (uint8_t *)&ntp_packet, sizeof(ntp_packet_t), &sockaddr_ntp,
                                 sizeof(struct sockaddr_in6),
                                 NULL, K_NO_WAIT, NULL);
    if (ret < 0)
    {
        LOG_ERR("Time update failed (%d)", ret);
    }
}

static void ntp_display_time(struct tm *time)
{

    // Print the time we got from the server, accounting for local timezone and conversion from UTC time.
    printk("%d-%d-%d\n%d:%d:%d\n%s\n\n", time->tm_year + 1900, time->tm_mon + 1, time->tm_mday, time->tm_hour, time->tm_min, time->tm_sec, weekdays[time->tm_wday]);
    char formatted_date[10] = "";
    char formatted_time[9] = "";
    sprintf(formatted_date, "%d-%d-%d", time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
    sprintf(formatted_time, "%d:%d:%d", time->tm_hour, time->tm_min, time->tm_sec);
    
    /*
    oled_clearDisplay(sh1106);
    oled_printString(sh1106, weekdays[time->tm_wday], 0, 0, 16, false);
    oled_printString(sh1106, formatted_date, 0, 16, 16, false);
    oled_print7Seg_number(sh1106, formatted_time, 0, 40);
    oled_display(sh1106);
    */
}

void ntp_update_time()
{

    if (!first_updated || (((k_uptime_get_32() - lastUpdate) >= updateInterval) && !update_received))
        ntp_request_server();

    struct tm *time;
    if (update_received)
    {
        printk("Update received!\n");
        update_received = false;
        ntp_packet.txTm_s = ntohl(ntp_packet.txTm_s); // Time-stamp seconds.
        ntp_packet.txTm_f = ntohl(ntp_packet.txTm_f); // Time-stamp fraction of a second.

        epochTime = (time_t)(ntp_packet.txTm_s - NTP_TIMESTAMP_DELTA + nepal_offset);
        time = gmtime((const time_t *)&epochTime);
    }
    else
    {
        if (first_updated)
        {
            time_t epochTimeTemp = epochTime + ((k_uptime_get_32() - lastUpdate) / 1000);
            time = gmtime((const time_t *)&epochTimeTemp);
        }
        else
            return;
    }
    ntp_display_time(time);
}

static void ntp_clock_process(void)
{

    if (!ntp_clock_get_context(&ntp_udp))
    {
        LOG_ERR("Cannot get network contexts");
        return;
    }

    ntp_setup_callback(ntp_udp);

    k_sem_take(&ntp_quit_lock, K_FOREVER);

    LOG_INF("Stopping ntp clock process");

    net_context_put(ntp_udp);
}

void ntp_clock_init(void)
{
    LOG_INF("Initializing NTP Clock");

    k_sem_init(&ntp_quit_lock, 0, K_SEM_MAX_LIMIT);

    k_thread_create(&ntp_thread, ntp_thread_stack, NTP_THREAD_STACKSIZE,
                    (k_thread_entry_t)ntp_clock_process,
                    NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
}