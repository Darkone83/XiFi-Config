#include "send_cmd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lwip/sockets.h>
#include <hal/debug.h>  // For debugPrint()

#define XIFI_CMD_PORT 1337   // Set your XiFi HTTP port here

void ascii_to_hex(const char* ascii, char* hexbuf, int hexbufsize) {
    int len = 0;
    for (; *ascii && len < (hexbufsize-2); ascii++, len+=2) {
        snprintf(hexbuf+len, 3, "%02X", (unsigned char)*ascii);
    }
    hexbuf[len] = 0;
}

bool send_cmd(const char* ip, const char* cmd_hex, const char* hex_arg) {
    if (!ip || !cmd_hex) {
        return false;
    }
    char url[256];
    if (hex_arg && hex_arg[0])
        snprintf(url, sizeof(url), "GET /cmd?hex=%s%s HTTP/1.0\r\nHost: %s\r\n\r\n", cmd_hex, hex_arg, ip);
    else
        snprintf(url, sizeof(url), "GET /cmd?hex=%s HTTP/1.0\r\nHost: %s\r\n\r\n", cmd_hex, ip);


    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(XIFI_CMD_PORT);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return false;
    }
    int sent = send(sock, url, strlen(url), 0);
    closesocket(sock);
    return (sent == (int)strlen(url));
}
