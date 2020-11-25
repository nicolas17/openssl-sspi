// SPDX-FileCopyrightText: 2020 Nicolás Alvarez <nicolas.alvarez@gmail.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifdef WIN32
# define _WIN32_WINNT 0x0600
#endif

#include "opensslclient.h"

#ifdef WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
#endif

#include <cassert>
#include <cstdio>

int sendall(int sock, const char* data, size_t length) {
    int retval;
    size_t remaining = length;
    const char* ptr = data;

    while (remaining > 0) {
        retval = send(sock, ptr, remaining, 0);
        if (retval < 0) return retval;
        if (retval == 0) return retval;
        remaining -= retval;
        ptr += retval;
    }
    return length;
}

int main()
{
    int retval;
#ifdef WIN32
    WSADATA wsaData;
    retval = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (retval != 0) {
        fprintf(stderr, "WSAStartup failed: %d", retval);
        return 1;
    }
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(44330);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock>=0);
    retval = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (retval != 0) {
        printf("Failed to connect\n");
        return 1;
    }
    printf("Connected\n");

    OpenSSLClient client;

    bool negotiating=true;

    while (true) {
        if (negotiating) {
            printf("We're still connecting, calling do_connect\n");
            if (client.do_connect()) {
                printf("Finished negotiating, flipping bool to false\n");
                negotiating=false;

                printf("Sending initial data\n");
                std::string dummy_data;
                for(size_t i=0; i<17000; ++i) {
                    dummy_data += "abcdefghijklmnopqrstuvwxyz"[i%26];
                }
                client.send_data(dummy_data);
            }
        }

        while (auto to_send = client.data_to_send()) {
            printf("SSL session tells us to send %zu bytes\n", to_send->length());
            sendall(sock, to_send->data(), to_send->length());
        }

        char buf[16384];
        printf("Receiving...\n");
        retval = recv(sock, buf, sizeof(buf), 0);
        if (retval <= 0) return 1;

        printf("Got %zu bytes from network\n", retval);
        client.receive_data(std::string(buf, retval));

        if (!negotiating) {
            while (auto recv_data = client.data_received()) {
                printf("Got %zu bytes of data: '%s'\n", recv_data->length(), recv_data->c_str());
            }
        }
    }

}
