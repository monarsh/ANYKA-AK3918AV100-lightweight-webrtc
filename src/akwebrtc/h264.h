#ifndef AKWEBRTC_H264_H
#define AKWEBRTC_H264_H

#include <stddef.h>
#include <stdint.h>

/*
 * Find the NAL unit that starts at or after `pos` in an Annex-B buffer.
 * On success sets *nal (offset of the NAL header byte) and *nal_len, and
 * returns the offset of the next start code (or len). Returns `pos`
 * unchanged when no further NAL exists.
 */
static inline size_t h264_find_start(const uint8_t *b, size_t len, size_t pos, size_t *sc_len)
{
    for (size_t i = pos; i + 3 <= len; i++) {
        if (b[i] == 0 && b[i + 1] == 0) {
            if (b[i + 2] == 1) {
                *sc_len = 3;
                return i;
            }
            if (i + 4 <= len && b[i + 2] == 0 && b[i + 3] == 1) {
                *sc_len = 4;
                return i;
            }
        }
    }
    *sc_len = 0;
    return len;
}

static inline size_t h264_next_nal(const uint8_t *b, size_t len, size_t pos,
                                   size_t *nal, size_t *nal_len)
{
    size_t sc;
    size_t s = h264_find_start(b, len, pos, &sc);
    if (s >= len)
        return pos;
    size_t hdr = s + sc;
    size_t sc2;
    size_t e = h264_find_start(b, len, hdr, &sc2);
    *nal = hdr;
    *nal_len = e - hdr;
    return e;
}

/* 1 if the access unit contains an IDR slice (or SPS, which precedes one). */
static inline int h264_is_key(const uint8_t *b, size_t len)
{
    size_t p = 0, nal, nal_len;
    for (;;) {
        size_t n = h264_next_nal(b, len, p, &nal, &nal_len);
        if (n == p)
            return 0;
        int t = b[nal] & 0x1f;
        if (t == 5 || t == 7)
            return 1;
        p = n;
    }
}

#endif
