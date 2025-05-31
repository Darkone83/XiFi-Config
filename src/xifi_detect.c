// xifi_detect.c
#include "xifi_detect.h"
#include <SDL.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <string.h>
#include <stdio.h>

#define XIFI_PORT 19784
#define BROADCAST_IP "255.255.255.255"
#define DETECTION_INTERVAL_MS 2000

static volatile int detected = 0;
static volatile int detection_running = 0;
static char xifi_ip[32] = "Unavailable";
static char detect_debug[128] = "Not started";

static int WaitForIP(void) {
    struct netif* nif = netif_default;
    if (!nif) {
        snprintf(xifi_ip, sizeof(xifi_ip), "No NIC");
        return 0;
    }
    if (!ip_addr_isany_val(nif->ip_addr)) {
        snprintf(xifi_ip, sizeof(xifi_ip), "%s", ipaddr_ntoa(&nif->ip_addr));
        return 1;
    }
    dhcp_start(nif);
    uint32_t start = SDL_GetTicks();
    while (ip_addr_isany_val(nif->ip_addr)) {
        if ((SDL_GetTicks() - start) > 20000) {
            snprintf(xifi_ip, sizeof(xifi_ip), "No DHCP");
            return 0;
        }
        SDL_Delay(100);
    }
    snprintf(xifi_ip, sizeof(xifi_ip), "%s", ipaddr_ntoa(&nif->ip_addr));
    return 1;
}

static int DetectThread(void* param) {
    if (!WaitForIP()) {
        detected = 0;
        detection_running = 0;
        snprintf(detect_debug, sizeof(detect_debug), "No IP");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        snprintf(detect_debug, sizeof(detect_debug), "Sock fail: %d", sock);
        detection_running = 0;
        return 1;
    }

    // Enable broadcast
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(XIFI_PORT);
    addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    struct sockaddr_in from = {0};
    socklen_t fromlen = sizeof(from);

    detected = 0;
    snprintf(detect_debug, sizeof(detect_debug), "Started");

    while (detection_running) {
        // Send discovery packet
        const char* msg = "XiFi?";
        int sent = sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));
        snprintf(detect_debug, sizeof(detect_debug), "Discovery sent: %d", sent);

        // Wait for reply (non-blocking, 200ms timeout)
        struct timeval tv = {0, 200 * 1000};
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(sock, &readset);
        int r = select(sock + 1, &readset, NULL, NULL, &tv);

        if (r > 0 && FD_ISSET(sock, &readset)) {
            char buf[64];
            int got = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &fromlen);
            if (got > 0) {
                buf[got] = 0;
                if (strstr(buf, "XiFi: PRESENT")) {
                    snprintf(xifi_ip, sizeof(xifi_ip), "%s", inet_ntoa(from.sin_addr));
                    detected = 1;
                    snprintf(detect_debug, sizeof(detect_debug), "REPLY: %s [%s]", buf, xifi_ip);
                    break;
                } else {
                    snprintf(detect_debug, sizeof(detect_debug), "Reply ignored: %s", buf);
                }
            } else {
                snprintf(detect_debug, sizeof(detect_debug), "Recv fail: %d", got);
            }
        } else {
            snprintf(detect_debug, sizeof(detect_debug), "No reply (r=%d)", r);
        }

        SDL_Delay(DETECTION_INTERVAL_MS);
    }

    closesocket(sock);
    detection_running = 0;
    return 0;
}

void XiFi_StartDetectionThread(unsigned interval_ms) {
    if (detection_running) return;
    detection_running = 1;
    detected = 0;
    SDL_CreateThread(DetectThread, "XiFiDetect", NULL);
}

int XiFi_IsPresent(void) {
    return detected;
}

const char* XiFi_GetIP(void) {
    return xifi_ip;
}

const char* XiFi_GetDebug(void) {
    return detect_debug;
}
