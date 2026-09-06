#include "network_simulator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static float g_loss_rate = 0.0f;
static float g_corrupt_rate = 0.0f;

void init_network_simulator(float loss_rate, float corrupt_rate) {
    g_loss_rate = loss_rate;
    g_corrupt_rate = corrupt_rate;
    srand((unsigned int)time(NULL));
}

static float get_random_prob(void) {
    return (float)rand() / (float)RAND_MAX;
}

ssize_t simulated_sendto(int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr *dest_addr, socklen_t addrlen) {
    // Giả lập rớt gói (Packet Loss)
    if (g_loss_rate > 0.0f && get_random_prob() < g_loss_rate) {
        printf("[SIMULATOR] >> Gói tin bị DROP (Loss)!\n");
        return (ssize_t)len; // Báo thành công ảo để bên gửi chờ timeout
    }

    // Giả lập lỗi bit (Bit Corruption)
    if (g_corrupt_rate > 0.0f && get_random_prob() < g_corrupt_rate && len > 0) {
        unsigned char *corrupted_buf = malloc(len);
        if (corrupted_buf != NULL) {
            memcpy(corrupted_buf, buf, len);

            // Đảo 1 bit ngẫu nhiên trong gói
            size_t target_byte = rand() % len;
            int target_bit = rand() % 8;
            corrupted_buf[target_byte] ^= (1 << target_bit);

            printf("[SIMULATOR] >> Gói tin bị CORRUPTED (Lỗi bit tại byte %zu)!\n", target_byte);

            ssize_t sent = sendto(sockfd, corrupted_buf, len, flags, dest_addr, addrlen);
            free(corrupted_buf);
            return sent;
        }
    }

    return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}