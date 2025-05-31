#ifndef SEND_CMD_H
#define SEND_CMD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Send HTTP command to XiFi device. hex_arg may be NULL.
bool send_cmd(const char* ip, const char* cmd_hex, const char* hex_arg);

// Convert ASCII string (up to 32 chars) to uppercase hex string.
void ascii_to_hex(const char* ascii, char* hexbuf, int hexbufsize);

#ifdef __cplusplus
}
#endif

#endif // SEND_CMD_H
