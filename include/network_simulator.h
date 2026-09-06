#ifndef NETWORK_SIMULATOR_H
#define NETWORK_SIMULATOR_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

void init_network_simulator(float loss_rate, float corrupt_rate);

ssize_t simulated_sendto(int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr *dest_addr, socklen_t addrlen);

#endif