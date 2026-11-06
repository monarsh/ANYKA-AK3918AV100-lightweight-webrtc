/*
 * Viewer sessions on top of libpeer. One PeerConnection plus one small
 * thread (running peer_connection_loop) per viewer. Video and audio are
 * fanned out from the source threads to every connected viewer.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "peer.h"

#include "log.h"
#include "session.h"
#include "source.h"

#define PCMA_PACKET_BYTES   160     /* 20 ms at 8 kHz */
#define ANSWER_TIMEOUT_S    30      /* drop offers nobody answered */
#define CONNECT_TIMEOUT_S   30      /* drop sessions stuck in ICE/DTLS */
#define LOOP_STACK_SIZE     (128 * 1024)

struct session {
    int id;
    PeerConnection *pc;
    pthread_t thread;
    volatile int run;
    volatile PeerConnectionState state;
    volatile int need_key;
    int answered;
    time_t created;
    time_t answered_at;
    uint8_t abuf[PCMA_PACKET_BYTES];
    size_t alen;
    struct session *next;
};

static struct session_config g_cfg;
static struct session *g_list;
static int g_next_id = 1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static time_t now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void on_state(PeerConnectionState state, void *user)
{
    struct session *s = user;
    s->state = state;
    LOG_I("session %d: %s", s->id, peer_connection_state_to_string(state));
    if (state == PEER_CONNECTION_CONNECTED) {
        s->need_key = 1;
        source_request_idr();
    }
}

static void on_keyframe_request(void *user)
{
    (void)user;
    source_request_idr();
}

static void *loop_thread(void *arg)
{
    struct session *s = arg;
    while (s->run) {
        peer_connection_loop(s->pc);
        usleep(1000);
    }
    return NULL;
}

void session_init(const struct session_config *cfg)
{
    g_cfg = *cfg;
    if (g_cfg.max_sessions <= 0)
        g_cfg.max_sessions = 1;
}

int session_count(void)
{
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (struct session *s = g_list; s; s = s->next)
        n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

static void session_free(struct session *s)
{
    if (s->run) {
        s->run = 0;
        pthread_join(s->thread, NULL);
    }
    peer_connection_destroy(s->pc);
    LOG_I("session %d: removed", s->id);
    free(s);
}

int session_create(char **offer_sdp)
{
    struct session *s;
    PeerConfiguration pcfg;
    pthread_attr_t attr;
    const char *sdp;

    *offer_sdp = NULL;
    if (session_count() >= g_cfg.max_sessions) {
        LOG_W("viewer limit (%d) reached", g_cfg.max_sessions);
        return -1;
    }

    s = calloc(1, sizeof(*s));
    if (!s)
        return -1;

    memset(&pcfg, 0, sizeof(pcfg));
    if (g_cfg.stun_url && g_cfg.stun_url[0])
        pcfg.ice_servers[0].urls = g_cfg.stun_url;
    pcfg.video_codec = CODEC_H264;
    pcfg.audio_codec = g_cfg.audio ? CODEC_PCMA : CODEC_NONE;
    pcfg.datachannel = DATA_CHANNEL_NONE;
    pcfg.on_request_keyframe = on_keyframe_request;
    pcfg.user_data = s;

    s->pc = peer_connection_create(&pcfg);
    if (!s->pc) {
        free(s);
        return -1;
    }
    peer_connection_oniceconnectionstatechange(s->pc, on_state);

    sdp = peer_connection_create_offer(s->pc);
    if (!sdp || !sdp[0] || !(*offer_sdp = strdup(sdp))) {
        peer_connection_destroy(s->pc);
        free(s);
        return -1;
    }

    s->created = now_s();
    s->run = 1;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, LOOP_STACK_SIZE);
    if (pthread_create(&s->thread, &attr, loop_thread, s) != 0) {
        pthread_attr_destroy(&attr);
        s->run = 0;
        free(*offer_sdp);
        *offer_sdp = NULL;
        peer_connection_destroy(s->pc);
        free(s);
        return -1;
    }
    pthread_attr_destroy(&attr);

    pthread_mutex_lock(&g_lock);
    s->id = g_next_id++;
    if (g_next_id <= 0)
        g_next_id = 1;
    s->next = g_list;
    g_list = s;
    pthread_mutex_unlock(&g_lock);

    LOG_I("session %d: offer created", s->id);
    return s->id;
}

int session_set_answer(int id, const char *sdp)
{
    int ret = -1;
    pthread_mutex_lock(&g_lock);
    for (struct session *s = g_list; s; s = s->next) {
        if (s->id == id && !s->answered) {
            s->answered = 1;
            s->answered_at = now_s();
            peer_connection_set_remote_description(s->pc, sdp, SDP_TYPE_ANSWER);
            ret = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return ret;
}

static struct session *unlink_where(int (*pred)(struct session *, void *), void *arg)
{
    struct session *out = NULL;
    pthread_mutex_lock(&g_lock);
    struct session **pp = &g_list;
    while (*pp) {
        struct session *s = *pp;
        if (pred(s, arg)) {
            *pp = s->next;
            s->next = out;
            out = s;
        } else {
            pp = &s->next;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return out;
}

static void free_list(struct session *s)
{
    while (s) {
        struct session *n = s->next;
        session_free(s);
        s = n;
    }
}

static int pred_id(struct session *s, void *arg)
{
    return s->id == *(int *)arg;
}

static int pred_dead(struct session *s, void *arg)
{
    time_t t = *(time_t *)arg;
    switch (s->state) {
    case PEER_CONNECTION_FAILED:
    case PEER_CONNECTION_CLOSED:
    case PEER_CONNECTION_DISCONNECTED:
        return 1;
    case PEER_CONNECTION_CONNECTED:
        return 0;
    default:
        if (!s->answered)
            return t - s->created > ANSWER_TIMEOUT_S;
        return t - s->answered_at > CONNECT_TIMEOUT_S;
    }
}

static int pred_all(struct session *s, void *arg)
{
    (void)s;
    (void)arg;
    return 1;
}

int session_close(int id)
{
    struct session *s = unlink_where(pred_id, &id);
    if (!s)
        return -1;
    free_list(s);
    return 0;
}

void session_reap(void)
{
    time_t t = now_s();
    free_list(unlink_where(pred_dead, &t));
}

void session_close_all(void)
{
    free_list(unlink_where(pred_all, NULL));
}

void session_push_video(const uint8_t *data, size_t len, int is_key)
{
    pthread_mutex_lock(&g_lock);
    for (struct session *s = g_list; s; s = s->next) {
        if (s->state != PEER_CONNECTION_CONNECTED)
            continue;
        if (s->need_key) {
            if (!is_key)
                continue; /* the browser cannot decode until an IDR */
            s->need_key = 0;
        }
        peer_connection_send_video(s->pc, data, len);
    }
    pthread_mutex_unlock(&g_lock);
}

void session_push_audio(const uint8_t *data, size_t len)
{
    pthread_mutex_lock(&g_lock);
    for (struct session *s = g_list; s; s = s->next) {
        if (s->state != PEER_CONNECTION_CONNECTED)
            continue;
        /* Repacketise into 20 ms PCMA packets, whatever the encoder's
         * frame size is; libpeer advances the RTP clock by 20 ms each. */
        size_t off = 0;
        while (off < len) {
            size_t n = PCMA_PACKET_BYTES - s->alen;
            if (n > len - off)
                n = len - off;
            memcpy(s->abuf + s->alen, data + off, n);
            s->alen += n;
            off += n;
            if (s->alen == PCMA_PACKET_BYTES) {
                peer_connection_send_audio(s->pc, s->abuf, PCMA_PACKET_BYTES);
                s->alen = 0;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
}
