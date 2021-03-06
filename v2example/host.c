#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "util.h"
#include "salt_v2.h"
#include "salt_io.h"


static void *connection_handler(void *context);
static void *write_handler(void *context);

static uint8_t host_sk_sec[64] = {
    0x7a, 0x77, 0x2f, 0xa9, 0x01, 0x4b, 0x42, 0x33,
    0x00, 0x07, 0x6a, 0x2f, 0xf6, 0x46, 0x46, 0x39,
    0x52, 0xf1, 0x41, 0xe2, 0xaa, 0x8d, 0x98, 0x26,
    0x3c, 0x69, 0x0c, 0x0d, 0x72, 0xee, 0xd5, 0x2d,
    0x07, 0xe2, 0x8d, 0x4e, 0xe3, 0x2b, 0xfd, 0xc4,
    0xb0, 0x7d, 0x41, 0xc9, 0x21, 0x93, 0xc0, 0xc2,
    0x5e, 0xe6, 0xb3, 0x09, 0x4c, 0x62, 0x96, 0xf3,
    0x73, 0x41, 0x3b, 0x37, 0x3d, 0x36, 0x16, 0x8b
};

struct clientInfo {
    int sock_fd;
    char ip_addr[16];
    struct sockaddr_in client;
    salt_channel_t channel;
};

int main(int argc , char *argv[])
{

    (void) argc;
    (void) argv;

    int socket_desc;
    int client_sock;
    

    struct clientInfo *client_info;

    int c;
    struct sockaddr_in server;
    setbuf(stdout, NULL);

    socket_desc = socket(AF_INET , SOCK_STREAM , 0);

    if (socket_desc == -1)
    {
        printf("Could not create socket");
    }

    int reuse = 1;
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        printf("setsockopt(SO_REUSEADDR) failed");
        return 1;
    }

    puts("Socket created");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(2033);

    if(bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        perror("bind failed. Error");
        return 1;
    }
    puts("bind done");

    listen(socket_desc , 3);

    while (1)
    {
        puts("Waiting for incoming connections...");
        c = sizeof(struct sockaddr_in);

        //puts("\033[A\33[2K");

        client_info = malloc(sizeof(struct clientInfo));

        client_info->sock_fd = accept(socket_desc,(struct sockaddr*)&client_info->client, (socklen_t*)&c);
        puts("Connection accepted");

        snprintf(client_info->ip_addr, 16, "%d.%d.%d.%d",
                            client_info->client.sin_addr.s_addr & 0xFF,
                            ((client_info->client.sin_addr.s_addr&0xFF00)>>8),
                            ((client_info->client.sin_addr.s_addr&0xFF0000)>>16),
                            ((client_info->client.sin_addr.s_addr&0xFF000000)>>24));

        pthread_t salt_thread;

        if(pthread_create(&salt_thread, NULL,  connection_handler, (void*) client_info) < 0)
        {
            puts("could not create thread");
            return 1;
        }

        printf("Waiting for client to disconnect.\r\n");

    }

    if (client_sock < 0)
    {
        perror("accept failed");
        return 1;
    }

    pthread_exit(NULL);
}

static void *connection_handler(void *context)
{
    //Get the socket descriptor
    struct clientInfo *client = (struct clientInfo *) context;
    int sock = client->sock_fd;
    salt_ret_t ret;
    uint32_t size;
    uint8_t hndsk_buffer[SALT_HNDSHK_BUFFER_SIZE];

    ret = salt_create(&client->channel, SALT_SERVER, my_write, my_read, NULL);
    assert(ret == SALT_SUCCESS);
    ret = salt_set_signature(&client->channel, host_sk_sec);
    assert(ret == SALT_SUCCESS);
    ret = salt_init_session(&client->channel, hndsk_buffer, sizeof(hndsk_buffer));
    assert(ret == SALT_SUCCESS);

    ret = salt_set_context(&client->channel, &client->sock_fd, &client->sock_fd);
    assert(ret == SALT_SUCCESS);
    ret = salt_handshake(&client->channel);

    while (ret != SALT_SUCCESS) {

        if (ret == SALT_ERROR) {
            printf("Salt error: 0x%02x\r\n", client->channel.err_code);
            printf("Salt error read: 0x%02x\r\n", client->channel.read_channel.err_code);
            printf("Salt error write: 0x%02x\r\n", client->channel.write_channel.err_code);
        }

        assert(ret != SALT_ERROR);

        salt_handshake(&client->channel);

    }

    printf("Salt handshake succeeded.\r\n");
    pthread_t write_thread;
    if(pthread_create(&write_thread, NULL,  write_handler, (void*) client) < 0)
    {
        puts("could not create write thread");
        pthread_exit(NULL);
    }

    do
    {
        memset(hndsk_buffer, 0, sizeof(hndsk_buffer));
        ret = salt_read(&client->channel, hndsk_buffer, &size, SALT_HNDSHK_BUFFER_SIZE);
        if (ret == SALT_SUCCESS)
        {
            printf("\33[2K\rclient (%s): %*.*s", client->ip_addr, 0, SALT_HNDSHK_BUFFER_SIZE-SALT_OVERHEAD_SIZE, &hndsk_buffer[SALT_OVERHEAD_SIZE]);
            printf("Enter message: ");
        }
    } while(ret == SALT_SUCCESS);


    close(sock);

    printf("Connection closed.\r\n");

    free(client);

    pthread_exit(NULL);
}

static void *write_handler(void *context)
{
    struct clientInfo *client = (struct clientInfo *) context;
    int tx_size;
    char tx_buffer[256];
    salt_ret_t ret_code;

    do
    {
        printf("Enter message: ");
        tx_size = read(0, &tx_buffer[SALT_OVERHEAD_SIZE], sizeof(tx_buffer)-SALT_OVERHEAD_SIZE);
        if (tx_size > 0) {
            printf("\033[A\33[2K\rhost: %*.*s", 0, tx_size, &tx_buffer[SALT_OVERHEAD_SIZE]);
            ret_code = salt_write(&client->channel, (uint8_t*) tx_buffer, tx_size + SALT_OVERHEAD_SIZE);
        }
    } while (ret_code == SALT_SUCCESS);


    pthread_exit(NULL);
}
