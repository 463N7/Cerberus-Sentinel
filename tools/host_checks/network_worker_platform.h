#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_FINISHED 0x10c
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
void vTaskDelete(void*);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);

/* Fakes deliberately do not use Windows sockets or any host network. */
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_RAW 3
#define IPPROTO_TCP 6
#define IPPROTO_ICMP 1
#define F_SETFL 1
#define O_NONBLOCK 2
#define SOL_SOCKET 1
#define SO_ERROR 2
typedef unsigned socklen_t;
struct in_addr {uint32_t s_addr;};
struct sockaddr {uint16_t family;uint8_t bytes[14];};
struct sockaddr_in {uint16_t sin_family,sin_port;struct in_addr sin_addr;};
struct timeval {long tv_sec,tv_usec;};
typedef struct {unsigned bits;} fd_set;
#define FD_ZERO(s) ((s)->bits=0)
#define FD_SET(fd,s) ((s)->bits|=1U<<(fd))
#define socket fake_socket
#define close fake_close
#define bind fake_bind
#define connect fake_connect
#define select fake_select
#define recv fake_recv
#define recvfrom fake_recvfrom
#define sendto fake_sendto
#define getsockopt fake_getsockopt
#define fcntl fake_fcntl
#define htons(x) ((uint16_t)(((uint16_t)(x)>>8)|((uint16_t)(x)<<8)))
int socket(int,int,int);
int close(int);
int bind(int,const struct sockaddr*,socklen_t);
int connect(int,const struct sockaddr*,socklen_t);
int select(int,fd_set*,fd_set*,fd_set*,struct timeval*);
int recv(int,void*,size_t,int);
int recvfrom(int,void*,size_t,int,struct sockaddr*,socklen_t*);
int sendto(int,const void*,size_t,int,const struct sockaddr*,socklen_t);
int getsockopt(int,int,int,void*,socklen_t*);
int fcntl(int,int,int);

typedef int err_t;
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_IF -2
typedef struct {uint32_t addr;} ip4_addr_t;
struct eth_addr {uint8_t addr[6];};
struct netif {ip4_addr_t ip;bool up,link;};
#define netif_is_up(n) ((n)->up)
#define netif_is_link_up(n) ((n)->link)
#define netif_ip4_addr(n) (&(n)->ip)
typedef struct {int index;} esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char*);
int esp_netif_get_netif_impl_index(esp_netif_t*);
struct netif *netif_get_by_index(uint8_t);
int etharp_find_addr(struct netif*,const ip4_addr_t*,struct eth_addr**,const ip4_addr_t**);
err_t etharp_request(struct netif*,const ip4_addr_t*);
err_t tcpip_callback_wait(void(*)(void*),void*);
