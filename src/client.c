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

#define BUFFER_SIZE 512
#define ADDRESS "127.0.0.1"
#define PATH_SEND_CLIENT "/home/penitent/project/RFTU/client/client_send.txt"
#define MAX_RETRIES 5

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

// Client UPLOAD
void handle_send(int client_fd, struct sockaddr_in *server_addr, socklen_t addr_len){
    int file_fd = open(PATH_SEND_CLIENT, O_RDONLY | O_CREAT);
    if(file_fd < 0){
        perror("[Client] Loi mo file de gui!");
        return;
    }

    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    Packet send_packet, ack_packet;
    memset(&send_packet, 0, sizeof(Packet));
    memset(&ack_packet, 0 , sizeof(Packet));
    uint32_t seq_num = 0;
    ssize_t bytes_read = 0;

    printf("[Client] Bat dau truyen file........\n");

    while((bytes_read = read(file_fd, send_packet.data, BUFFER_SIZE)) >= 0){
        send_packet.seq_num = seq_num;
        send_packet.data_len = bytes_read;
        send_packet.checksum = compute_packet_checksum(&send_packet);
        
        int check_ack = 0;
        int retries = 0;

        while(retries < MAX_RETRIES){
            ssize_t bytes_sent = sendto(client_fd, &send_packet,sizeof(Packet), 0, (struct sockaddr*)server_addr, addr_len);
            if(bytes_sent < 0){
                perror("[Client] Loi gui du lieu!");
                break;
            }

            socklen_t len = addr_len;
            ssize_t ack_bytes= recvfrom(client_fd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)server_addr, &len);
            if(ack_bytes > 0){
                if(compute_packet_checksum(&ack_packet) == ack_packet.checksum &&
                    ack_packet.is_ack == 1 && ack_packet.ack_num == seq_num){
                        check_ack = 1;
                        break;
                }
            }
            retries++;
            printf("[Client] Timeout/Loi ACK goi seq=%d, dang dui lai (%d/%d)...\n", seq_num, retries, MAX_RETRIES);
        }
        if(!check_ack){
            perror("[Client] Loi: Khong nhan duoc ACK!");
            break;
        }
        printf("[Client] Da gui mot goi seq=%d (%d bytes) & Nhan ACK thanh cong!\n", seq_num, send_packet.data_len);
        seq_num++;

        sleep(3); // Delay 3 seconds before sending the next packet
        if(bytes_read < BUFFER_SIZE) break;
    }
    struct timeval zero_timeout = {0, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &zero_timeout, sizeof(zero_timeout));
    
    close(file_fd);
    printf("[Client] Gui file hoan tat!\n");
}


//Client DOWNLOAD
void handle_recv(int client_fd, struct sockaddr_in *server_addr, socklen_t addr_len){
    char recv_path[256];
    snprintf(recv_path, sizeof(recv_path), "/home/penitent/project/RFTU/client/client_recv_%s_%d.txt", inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));
    
    int file_fd = open(recv_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(file_fd < 0){
        perror("[Client] Loi mo file de ghi du lieu truyen!");
        return;
    }

    Packet recv_packet, ack_packet;
    memset(&recv_packet, 0, sizeof(Packet));
    memset(&ack_packet, 0, sizeof(Packet));
    uint32_t expected_seq = 0;
    ssize_t bytes_written =0;

    printf("[Client] Bat dau nhan du lieu........\n");

    while(1){
        socklen_t len = addr_len;
        ssize_t bytes_received = recvfrom(client_fd, &recv_packet, sizeof(Packet), 0, (struct sockaddr*)server_addr, &len);
        if(bytes_received < 0){
            perror("[Client] Loi recvfrom!");
            break;
        }
        // Kiem tra Checsum
        if(compute_packet_checksum(&recv_packet) != recv_packet.checksum){
            printf("[Client] Goi tin seq=%d bi loi Checksum! Huy goi.\n", recv_packet.seq_num);
            continue;
        }
        // Kiem tra Sequence Number
        if(recv_packet.seq_num == expected_seq){
            bytes_written = write(file_fd, recv_packet.data, recv_packet.data_len);
            if(bytes_written < 0){
                perror("[Client] Loi ghi du lieu vao file!");
                break;
            }
            printf("[Client] Nhan goi seq=%d - Da ghi %ld bytes vao file.\n", recv_packet.seq_num, bytes_written);
            expected_seq++;
        } else{
            printf("[Client] Nhan lai goi cu seq=%d. Gui lai ACK...\n", recv_packet.seq_num);
        }

        memset(&ack_packet, 0, sizeof(Packet));
        ack_packet.is_ack = 1;
        ack_packet.ack_num = recv_packet.seq_num;
        ack_packet.checksum = compute_packet_checksum(&ack_packet);

        sendto(client_fd, &ack_packet, sizeof(Packet), 0, (struct sockaddr*)server_addr, len);

        if(recv_packet.data_len < BUFFER_SIZE){
            break;
        }
    }
    close(file_fd);
    printf("[Client] Nhan file hoan tat!\n");
}

int main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(stderr,"Uasge %s <port>\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    int client_fd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];
    memset(&buffer, 0, sizeof(buffer));

    client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(client_fd < 0){
        perror("[Client] Loi tao socket!");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    if(inet_pton(AF_INET, ADDRESS, &server_addr.sin_addr) <= 0){
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    printf("[Client] Da ket noi toi server tai cong %d.\n", atoi(argv[1]));
    printf("Enter the option: \n");
    while(1){
        fgets(buffer, BUFFER_SIZE, stdin);
        ssize_t bytes_sent = sendto(client_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&server_addr, addr_len);
        if(strncmp(buffer, "EXIT", 4) == 0){
            printf("[Client] Da gui lenh EXIT, ket thuc ket noi!\n");
            break;
        }
        char response[64];
        memset(&response,  0, sizeof(response));
        struct sockaddr_in thread_addr;
        socklen_t thread_len = sizeof(thread_addr);
        ssize_t received_bytes = recvfrom(client_fd, response, sizeof(response) - 1, 0, (struct sockaddr*)&server_addr, &addr_len);
        if(received_bytes < 0){
            printf("[Client] khong nhan duoc phan hoi PORT tu Server!\n");
            continue;
        }
        response[received_bytes] = '\0';
        if(strncmp(response, "PORT:", 5) != 0){
            printf("[Client] Phan hoi khong hop le tu Server!: %s\n", response);
            continue;
        }
        int thread_port = atoi(response + 5);
        thread_addr = server_addr;
        thread_addr.sin_port = htons(thread_port);

        if(strncmp(buffer, "UPLOAD", 6) == 0){
            handle_send(client_fd, &thread_addr, sizeof(thread_addr));
        } else if(strncmp(buffer, "DOWNLOAD" , 8) == 0){
            handle_recv(client_fd, &thread_addr, sizeof(thread_addr));
        }
    }
    printf("[Client] Da dong ket noi toi Server!\n");
    close(client_fd);
    return 0;
}