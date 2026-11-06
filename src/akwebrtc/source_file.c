/*
 * File source: plays an H.264 Annex-B elementary stream in a loop at the
 * configured frame rate. Lets the WebRTC path be tested on a PC.
 *
 *   ffmpeg -f lavfi -i testsrc=size=640x360:rate=20 -t 10 \
 *          -c:v libx264 -profile:v baseline -g 40 -bf 0 test.h264
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "h264.h"
#include "log.h"
#include "source.h"

static uint8_t *g_buf;
static size_t g_len;
static int g_fps;
static volatile int g_run;
static pthread_t g_thread;
static source_video_cb g_vcb;
static void *g_user;

static void sleep_until(struct timespec *t)
{
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL) != 0) {
    }
}

static void *file_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    long frame_ns = 1000000000L / g_fps;
    size_t pos = 0;

    while (g_run) {
        /* An access unit ends right before the next VCL NAL that starts a
         * new picture; collect NALs until we have passed one VCL NAL. */
        size_t start = pos, p = pos;
        int seen_vcl = 0, is_key = 0;
        for (;;) {
            size_t nal, nal_len;
            size_t next_p = h264_next_nal(g_buf, g_len, p, &nal, &nal_len);
            if (next_p == p) { /* end of file */
                p = g_len;
                break;
            }
            int type = g_buf[nal] & 0x1f;
            int vcl = (type >= 1 && type <= 5);
            if (seen_vcl && (vcl || type == 7 || type == 9)) {
                break; /* next AU starts at p */
            }
            if (vcl)
                seen_vcl = 1;
            if (type == 5)
                is_key = 1;
            p = next_p;
        }
        if (p > start)
            g_vcb(g_buf + start, p - start, is_key, g_user);
        pos = (p >= g_len) ? 0 : p;

        next.tv_nsec += frame_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        sleep_until(&next);
    }
    return NULL;
}

int source_start(const struct source_config *cfg, source_video_cb vcb,
                 source_audio_cb acb, void *user)
{
    (void)acb;
    const char *path = cfg->uri + 5; /* skip "file:" */
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_E("cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return -1;
    }
    g_buf = malloc((size_t)n);
    if (!g_buf || fread(g_buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(g_buf);
        g_buf = NULL;
        return -1;
    }
    fclose(f);
    g_len = (size_t)n;
    g_fps = cfg->fps > 0 ? cfg->fps : 20;
    g_vcb = vcb;
    g_user = user;
    g_run = 1;
    if (cfg->audio)
        LOG_W("file source has no audio; audio disabled");
    LOG_I("file source: %s (%zu bytes) at %d fps", path, g_len, g_fps);
    return pthread_create(&g_thread, NULL, file_thread, NULL);
}

void source_request_idr(void)
{
    /* Nothing to do: the file repeats its GOPs, a key frame comes soon. */
}

void source_stop(void)
{
    if (!g_run)
        return;
    g_run = 0;
    pthread_join(g_thread, NULL);
    free(g_buf);
    g_buf = NULL;
}
