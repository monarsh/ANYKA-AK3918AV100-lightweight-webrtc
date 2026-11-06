#ifndef AKWEBRTC_LOG_H
#define AKWEBRTC_LOG_H

#include <stdio.h>

#define LOG_E(fmt, ...) fprintf(stderr, "[akwebrtc] E: " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...) fprintf(stderr, "[akwebrtc] W: " fmt "\n", ##__VA_ARGS__)
#define LOG_I(fmt, ...) fprintf(stderr, "[akwebrtc] I: " fmt "\n", ##__VA_ARGS__)

#endif
