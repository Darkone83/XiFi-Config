#ifndef XIFI_DETECT_H
#define XIFI_DETECT_H

#ifdef __cplusplus
extern "C" {
#endif

// Start the detection thread (poll every interval_ms milliseconds)
void XiFi_StartDetectionThread(unsigned interval_ms);

// Returns 1 if detected, 0 if not detected
int XiFi_IsPresent(void);

// Returns the last detected IP as a string, or "Unavailable"
const char* XiFi_GetIP(void);

// Returns a debug string for on-screen diagnostics
const char* XiFi_GetDebug(void);

#ifdef __cplusplus
}
#endif

#endif // XIFI_DETECT_H
