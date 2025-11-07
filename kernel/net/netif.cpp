#include "netif.hpp"
#include "../utils/logger.hpp"
#include <string.h>

static netif_t* registered[8];
static int reg_count = 0;

extern "C" int netif_register(netif_t* nif) {
    if (!nif || reg_count >= (int)(sizeof(registered)/sizeof(registered[0]))) return -1;
    registered[reg_count++] = nif;
    hanacore::utils::log_ok_cpp("netif: registered %s", nif->name ? nif->name : "<noname>");
    return 0;
}

extern "C" int netif_send(netif_t* nif, const void* pkt, size_t len) {
    if (!nif || !nif->xmit) return -1;
    return nif->xmit(nif, pkt, len);
}

extern "C" void netif_poll() {
    // No real RX path yet; drivers should invoke rx callbacks when packets arrive.
    (void)registered;
}
