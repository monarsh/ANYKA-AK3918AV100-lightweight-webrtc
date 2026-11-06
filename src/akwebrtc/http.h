#ifndef AKWEBRTC_HTTP_H
#define AKWEBRTC_HTTP_H

/* Serve signalling and the player page until *running becomes 0. */
int http_serve(int port, volatile int *running);

#endif
