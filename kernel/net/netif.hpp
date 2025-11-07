#pragma once
#include <stddef.h>

typedef struct netif netif_t;

typedef void (*netif_rx_cb)(netif_t* iface, const void* pkt, size_t len);
typedef int  (*netif_xmit_cb)(netif_t* iface, const void* pkt, size_t len);

struct netif {
    const char* name;
    netif_rx_cb rx;
    netif_xmit_cb xmit;
    void* priv;
};

extern "C" {
// Register a network interface. Returns 0 on success.
int netif_register(netif_t* nif);
// Send a packet via the named interface
int netif_send(netif_t* nif, const void* pkt, size_t len);
// Poll interfaces for incoming packets (call their rx callbacks).
void netif_poll();
}
