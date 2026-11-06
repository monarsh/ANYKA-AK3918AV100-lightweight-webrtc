#ifndef AKWEBRTC_SESSION_H
#define AKWEBRTC_SESSION_H

#include <stddef.h>
#include <stdint.h>

struct session_config {
    int max_sessions;
    int audio;              /* offer a PCMA audio track */
    const char *stun_url;   /* e.g. "stun:stun.l.google.com:19302", or NULL */
};

void session_init(const struct session_config *cfg);

/*
 * Create a viewer session. The camera is the offerer (that is how libpeer
 * works), so this returns a malloc'd SDP offer that the browser answers.
 * Returns the session id (> 0), or -1 when full or on error.
 */
int session_create(char **offer_sdp);
int session_set_answer(int id, const char *sdp);
int session_close(int id);
int session_count(void);

/* Drop failed, closed or never-answered sessions. Call periodically. */
void session_reap(void);
void session_close_all(void);

/* Media fan-out, called from the source threads. */
void session_push_video(const uint8_t *data, size_t len, int is_key);
void session_push_audio(const uint8_t *data, size_t len);

#endif
