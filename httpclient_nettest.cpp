// SPDX-FileCopyrightText: 2020 Nicolás Alvarez <nicolas.alvarez@gmail.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "httpclient.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

int main() {
    HTTPClient client;

    client.make_get_request("/", {{"Host","example.com"}});

    int retval;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_aton("217.13.79.76", &addr.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock>=0);
    retval = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (retval != 0) {
        printf("Failed to connect\n");
        return 1;
    }

    while (true) {

        if (auto http_to_send = client.data_to_send()) {
            printf("Client tells us to send %zu bytes\n", http_to_send->length());
            sendall(sock, http_to_send->data(), http_to_send->length());
        }

        char buf[256];
        printf("Receiving...\n");
        retval = recv(sock, buf, sizeof(buf), 0);
        if (retval <= 0) return 1; // TODO error handling

        printf("Got %zu bytes from network\n", retval);
        client.receive_data(std::string(buf, retval));

        while (auto maybe_event = client.next_event()) {
            printf("Got event\n");
            auto& event_v = maybe_event.value();
            if (auto resp_event = std::get_if<HTTPClient::ResponseEvent>(&event_v)) {
                printf("Got response with code %d\n", resp_event->status_code);
            } else if (auto data_event = std::get_if<HTTPClient::DataEvent>(&event_v)) {
                ;
            }
        }
    }

}
