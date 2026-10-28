#ifndef MS2X_LINUX_H
#define MS2X_LINUX_H
#include "ms2x_session.h"
enum { MS2X_LINUX_PASSIVE, MS2X_LINUX_PING, MS2X_LINUX_UPDATE, MS2X_LINUX_RESOURCES, MS2X_LINUX_BOOT_QUERY };
int ms2x_linux_open(const char *interface, int write_mode);
void ms2x_linux_close(void);
void ms2x_linux_stop(void); /* async-signal-safe flag only */
#endif