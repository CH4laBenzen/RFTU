#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<fcntl.h>
#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<pthread.h>
#include"network_simulator.h"

#define BUFFER_SIZE 512
#define PATH_SEND_SERVER "/home/penitent/project/RFTU/server/server_send.txt"
#define MAX_RETRIES 5

typedef enum {
    UPLOAD,
    DOWNLOAD
} CommandType;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    CommandType cmd;
} ThreadArguments;

typedef struct {
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t is_ack;
    uint16_t data_len;
    uint16_t checksum;
    char data[BUFFER_SIZE];
} Packet;

uint16_t calculate_checksum(void *data, size_t length){
    char *dt = (char*)data;
    uint32_t acc = 0xffff;

    for(size_t i = 0; i + 1 < length; i += 2){
        uint16_t word;
        memcpy(&word, dt + i, 2);
        acc += ntohs(word);
        if (acc > 0xffff) acc -= 0xffff;
    }
    if(length & 1){
        uint16_t word = 0;
        memcpy(&word, dt + length - 1, 1);
        acc += ntohs(word);
        if(acc > 0xffff) acc -= 0xffff;
    }
    return htons(~acc);
}

uint16_t compute_packet_checksum(Packet *packet){
    uint16_t origin = packet->checksum;
    packet->checksum = 0;
    uint16_t cksum = calculate_checksum(packet, sizeof(Packet));
    packet->checksum = origin;
    return cksum;
}

//Ham gui File
void handle_send(int server_fd, struct sockaddr_in *client_addr, socklen_t addr_len){
    int file_fd = open(PATH_SEND_SERVER, O_RDONLY);
    if(file_fd < 0){
        perror("[Server] Loi mo file de gui!");
        return;
    }

    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    Packet send_packet, ack_packet;
    memset(&send_packet, 0, sizeof(Packet));
    memset(&ack_packet, 0 , sizeof(Packet));
    uint32_t seq_num = 0;
    ssize_t bytes_read = 0;

    printf("[Worker] Bat dau gui file toi %s:%d...\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));

    while((bytes_read = read(file_fd, send_packet.data, BUFFER_SIZE)) >= 0){
        send_packet.seq_num = seq_num;
        send_packet.data_len = bytes_read;
        send_packet.checksum = compute_packet_checksum(&send_packet);

        int check_ack = 0;
        int retries = 0;

        while(retries < MAX_RETRIES){
            ssize_t bytes_sent = simulated_sendto(server_fd, &send_packet, sizeof(Packet), 0, (struct sockaddr*)client_addr, addr_len);
            if(bytes_sent < 0){
                perror("[Server] Loi gui du lieu!");
                break;
            }

            socklen_t len = addr_len;
            ssize_t ack_bytes = recvfrom(server_fd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)client_addr, &len);
            if(ack_bytes > 0){
                if(compute_packet_checksum(&ack_packet) == ack_packet.checksum &&
                    ack_packet.is_ack == 1 && ack_packet.ack_num == seq_num){
                        check_ack = 1;
                        break;
                }
            }
            retries++;
            printf("[Server] Timeout/Loi ACK goi seq=%d, dang gui lai (%d/%d)...\n", seq_num, retries, MAX_RETRIES);
        }
        if(!check_ack){
            printf("[Server] Loi: Khong nhan duoc ACK tu Client. Huy gui file!\n");
            break;
        }
        printf("[Server] Da gui goi seq=%d (%d bytes) & Nhan ACK thanh cong!\n", seq_num, send_packet.data_len);
        seq_num++;

        if(bytes_read < BUFFER_SIZE) break;
    }
    close(file_fd);
    printf("[Server] Gui file toi %s:%d hoan tat!\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
}

// Ham nhan File
void handle_recv(int server_fd, struct sockaddr_in *client_addr, socklen_t addr_len){
    char recv_path[256];
    snprintf(recv_path, sizeof(recv_path), "/home/penitent/project/RFTU/server/server_recv_%s_%d.txt", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));

    int file_fd = open(recv_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(file_fd < 0){
        perror("[Server] Loi mo file de ghi du lieu!");
        return;
    }

    Packet recv_packet, ack_packet;
    memset(&recv_packet, 0, sizeof(Packet));
    memset(&ack_packet, 0, sizeof(Packet));
    uint32_t expected_seq = 0;
    ssize_t bytes_written = 0;

    printf("[Server] Bat dau nhan du lieu........\n");

    while(1){
        socklen_t len = addr_len;
        ssize_t bytes_received = recvfrom(server_fd, &recv_packet, sizeof(Packet), 0, (struct sockaddr*)client_addr, &len);
        if(bytes_received < 0){
            perror("[Server] Loi recvfrom!");
            break;
        }
        // Kiem tra Checksum
        if(compute_packet_checksum(&recv_packet) != recv_packet.checksum){
            printf("[Server] Goi tin seq=%d bi loi Checksum! Huy goi.\n", recv_packet.seq_num);
            continue;
        }
        // Kiem tra Sequence Number
        if(recv_packet.seq_num == expected_seq){
            bytes_written = write(file_fd, recv_packet.data,recv_packet.data_len);
            if(bytes_written < 0){
                perror("[Server] Loi ghi vao file!");
                break;
            }
            printf("[Server] Nhan goi seq=%d - Da ghi %ld bytes vao file.\n", recv_packet.seq_num, bytes_written);
            expected_seq++;
        } else{
            printf("[Server] Nhan lai goi cu seq=%d. Gui lai ACK...\n", recv_packet.seq_num);
        }

        memset(&ack_packet, 0, sizeof(Packet));
        ack_packet.is_ack = 1;
        ack_packet.ack_num = recv_packet.seq_num;
        ack_packet.checksum = compute_packet_checksum(&ack_packet);
        simulated_sendto(server_fd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)client_addr, len);

        if(recv_packet.data_len < BUFFER_SIZE){
            break;
        }
    }
    close(file_fd);
    printf("[Server] Nhan file toi %s:%d hoan tat!\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
}

void* thread_handle(void* arg){
    ThreadArguments *args = (ThreadArguments*)arg;
    if(args->cmd == UPLOAD){
        handle_recv(args->client_fd, &args->client_addr, args->addr_len);
    } else if(args->cmd == DOWNLOAD){
        handle_send(args->client_fd, &args->client_addr, args->addr_len);
    }
    close(args->client_fd);
    free(args);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(stderr, "Usage: %s <port>\n",argv[0]);
        exit(EXIT_FAILURE);
    }

    init_network_simulator(0.15f, 0.05f); // Giả lập rớt gói 15% và lỗi bit 5%

    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(server_fd < 0){
        perror("[Server] Loi tao socket!");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, addr_len);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_fd, (struct sockaddr*)&server_addr, addr_len) < 0){
        perror("[Server] Loi bind socket");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Server] cho ket noi tai cong %d!\n", atoi(argv[1]));

    while(1){
        socklen_t client_len = sizeof(client_addr);
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        if(bytes_received <= 0) continue;
        buffer[bytes_received] = '\0';
        CommandType cmd;

        if(strncmp(buffer, "UPLOAD", 6) == 0){
            cmd = UPLOAD;
        }
        else if(strncmp(buffer, "DOWNLOAD", 8) == 0){
            cmd = DOWNLOAD;
        }
        else if(strncmp(buffer, "EXIT", 4) == 0){
            printf("[Server] Client %s:%d da ngat ket noi!\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            break;
        } else{
            printf("[Server] Lệnh không hợp lệ!\n");
            continue;
        }
        // Tao socket moi cho Client
        int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(client_fd < 0){
            perror("[Server] Loi tao socket moi cho Client!");
            continue;
        }

        struct sockaddr_in client_addr_handle;
        memset(&client_addr_handle, 0, sizeof(client_addr_handle));
        client_addr_handle.sin_family = AF_INET;
        client_addr_handle.sin_port = htons(0);
        client_addr_handle.sin_addr.s_addr = INADDR_ANY;

        if(bind(client_fd, (struct sockaddr*)&client_addr_handle, sizeof(client_addr_handle)) < 0){
            perror("[Server] loi bind socket moi cho Client!");
            close(client_fd);
            continue;
        }

        socklen_t len = sizeof(client_addr_handle);
        getsockname(client_fd, (struct sockaddr*)&client_addr_handle, &len);
        int assigned_port = ntohs(client_addr_handle.sin_port);

        char response[64];
        snprintf(response, sizeof(response), "PORT:%d", assigned_port);
        sendto(server_fd, response, strlen(response), 0, (struct sockaddr*)&client_addr, client_len);

        ThreadArguments *args = malloc(sizeof(ThreadArguments));
        args->client_fd = client_fd;
        args->client_addr = client_addr;
        args->addr_len = client_len;
        args->cmd = cmd;

        pthread_t thread_id;
        if(pthread_create(&thread_id, NULL, thread_handle, (void*)args) == 0){
            pthread_detach(thread_id);
        } else{
            perror("[Server] Loi tao thread!");
            close(client_fd);
            free(args);
        }
    }
    close(server_fd);
    return 0;
}