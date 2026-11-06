/*
 * akwebrtc - lightweight WebRTC streamer for Anyka AK3918AV100 cameras.
 *
 * Camera H.264 (+ optional G.711A) -> libpeer (ICE/DTLS/SRTP) -> browser.
 * Signalling is plain HTTP on the camera itself; see http.c.
 */
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "peer.h"

#include "http.h"
#include "log.h"
#include "session.h"
#include "source.h"

static volatile int g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void on_video(const uint8_t *data, size_t len, int is_key, void *user)
{
    (void)user;
    session_push_video(data, len, is_key);
}

static void on_audio(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    session_push_audio(data, len);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  -s SOURCE   'ak' (camera, default) or 'file:/path/stream.h264'\n"
            "  -i FILE     ISP/sensor config (default /etc/isp/isp.conf)\n"
            "  -c CHN      VI channel to encode: 0 main, 1 sub (default 1)\n"
            "  -W WIDTH    encode width  (default 640)\n"
            "  -H HEIGHT   encode height (default 360)\n"
            "  -f FPS      frame rate (default 15; must match libpeer VIDEO_FPS)\n"
            "  -b KBPS     H.264 bitrate (default 512)\n"
            "  -a          enable microphone audio (G.711 A-law)\n"
            "  -p PORT     HTTP signalling port (default 8080)\n"
            "  -m N        max simultaneous viewers (default 2)\n"
            "  -S URL      STUN server, e.g. stun:stun.l.google.com:19302\n"
            "              (default none: LAN only)\n",
            prog);
}

int main(int argc, char **argv)
{
    struct source_config src = {
        .uri = "ak",
        .isp_conf = "/etc/isp/isp.conf",
        .vi_chn = 1,
        .width = 640,
        .height = 360,
        .fps = 15,
        .kbps = 512,
        .audio = 0,
    };
    struct session_config ses = {.max_sessions = 2, .audio = 0, .stun_url = NULL};
    int port = 8080;
    int opt;

    while ((opt = getopt(argc, argv, "s:i:c:W:H:f:b:ap:m:S:h")) != -1) {
        switch (opt) {
        case 's': src.uri = optarg; break;
        case 'i': src.isp_conf = optarg; break;
        case 'c': src.vi_chn = atoi(optarg); break;
        case 'W': src.width = atoi(optarg); break;
        case 'H': src.height = atoi(optarg); break;
        case 'f': src.fps = atoi(optarg); break;
        case 'b': src.kbps = atoi(optarg); break;
        case 'a': src.audio = 1; break;
        case 'p': port = atoi(optarg); break;
        case 'm': ses.max_sessions = atoi(optarg); break;
        case 'S': ses.stun_url = optarg; break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }
    if (src.fps <= 0 || src.width <= 0 || src.height <= 0 || src.kbps <= 0) {
        usage(argv[0]);
        return 1;
    }
#ifdef AKWEBRTC_FILE_SOURCE
    if (strncmp(src.uri, "file:", 5) != 0) {
        LOG_E("this build only supports -s file:PATH");
        return 1;
    }
#else
    if (strcmp(src.uri, "ak") != 0) {
        LOG_E("this build only supports -s ak");
        return 1;
    }
#endif
#ifdef LIBPEER_VIDEO_FPS
    if (src.fps != LIBPEER_VIDEO_FPS)
        LOG_W("-f %d differs from libpeer VIDEO_FPS %d: playback speed will be off",
              src.fps, LIBPEER_VIDEO_FPS);
#endif

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    peer_init();
    ses.audio = src.audio;
    session_init(&ses);

    if (source_start(&src, on_video, on_audio, NULL) != 0) {
        LOG_E("cannot start media source");
        peer_deinit();
        return 1;
    }

    http_serve(port, &g_running);

    session_close_all();
    source_stop();
    peer_deinit();
    LOG_I("bye");
    return 0;
}
