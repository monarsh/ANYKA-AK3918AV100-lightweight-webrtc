/*
 * Media source interface.
 *
 * A source produces H.264 Annex-B access units (one call per frame, with
 * start codes) and, optionally, G.711 A-law audio. Two implementations:
 *   source_ak.c   - Anyka AK3918AV100 SDK (VI -> VENC, AI -> AENC)
 *   source_file.c - loops an .h264 file; for host testing without hardware
 */
#ifndef AKWEBRTC_SOURCE_H
#define AKWEBRTC_SOURCE_H

#include <stddef.h>
#include <stdint.h>

typedef void (*source_video_cb)(const uint8_t *data, size_t len, int is_key, void *user);
typedef void (*source_audio_cb)(const uint8_t *data, size_t len, void *user);

struct source_config {
    const char *uri;        /* "ak" or "file:/path/to/stream.h264" */
    const char *isp_conf;   /* Anyka ISP/sensor config file */
    int vi_chn;             /* Anyka: 0 = main channel, 1 = sub channel */
    int width;
    int height;
    int fps;
    int kbps;
    int audio;              /* 1 = capture G.711A 8 kHz mono */
};

int source_start(const struct source_config *cfg, source_video_cb vcb,
                 source_audio_cb acb, void *user);
void source_request_idr(void);
void source_stop(void);

#endif
