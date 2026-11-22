#pragma once
// Host stand-in for lwIP's socket API: the calls ProxyHook.cpp makes, mapped to POSIX.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define lwip_getsockopt getsockopt
#define lwip_setsockopt setsockopt
#define lwip_send send
#define lwip_recv recv
