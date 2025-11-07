#include "netif.hpp"
#include "../utils/logger.hpp"
#include <string.h>

static int loop_xmit(netif_t* nif, const void* pkt, size_t len) {
    // Echo packet back immediately via rx callback if present
    if (nif->rx) nif->rx(nif, pkt, len);
    hanacore::utils::log_info_cpp("loopback: echoed %u bytes", (unsigned)len);
    return 0;
}

static void loop_recv_stub(netif_t* iface, const void* pkt, size_t len) {
    // higher layers would process the packet; for now just log
    hanacore::utils::log_info_cpp("loopback: recv %u bytes", (unsigned)len);
}

extern "C" void net_loopback_init() {
    static netif_t loop = { "lo", loop_recv_stub, loop_xmit, nullptr };
    netif_register(&loop);
}
