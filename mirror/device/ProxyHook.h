#pragma once
#if defined(CROSSPOINT_MIRROR)
// Phone proxy shim. While enabled, every TCP connection the firmware opens (HTTP, HTTPS, any library: they all end in
// lwip_connect after a lookup) leaves through the proxy the phone offered: the lookup of a host name returns a fake
// address, and connecting to it sends "CONNECT name:port" to the proxy. TLS then runs through the tunnel as before.
// Disabled, both hooks pass straight through. The two calls are intercepted with the linker (-Wl,--wrap=lwip_connect
// and
// --wrap=lwip_getaddrinfo in mirror/platformio_mirror.ini), so no upstream or SDK file changes.
#include <cstdint>

namespace mirror::proxyhook {

// Addresses are IPv4 in host byte order. Connections to the proxy itself and into the hotspot's own subnet are never
// tunnelled.
void enable(uint32_t proxyIp, uint16_t proxyPort, uint32_t subnet, uint32_t subnetMask);
void disable();  // also forgets the fake addresses
bool active();

}  // namespace mirror::proxyhook
#endif
