/*
 * Anyka AK3918AV100 source: VI -> VENC (H.264) and AI -> AENC (G.711A).
 *
 * Follows the vendor sample platform/sample/kd_video (AnyCloud39AV100 SDK
 * V1.12). VI channel 0 is the main path and channel 1 the sub path; one
 * of them is encoded for WebRTC (default: sub channel, which keeps both
 * the H.264 bitrate and the SRTP encryption cost low on the ARM926).
 */
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "ak_common.h"
#include "ak_common_audio.h"
#include "ak_common_video.h"
#include "ak_aenc.h"
#include "ak_ai.h"
#include "ak_mem.h"
#include "ak_thread.h"
#include "ak_venc.h"
#include "ak_vi.h"
#include "ak_video.h"

#include "h264.h"
#include "log.h"
#include "source.h"

#define FRAME_DEPTH 2
#define PCMA_RATE   AK_AUDIO_SAMPLE_RATE_8000

static source_video_cb g_vcb;
static source_audio_cb g_acb;
static void *g_user;
static volatile int g_run;

static int g_venc = -1;
static int g_ai = -1;
static int g_aenc = -1;
static int g_vi_chn = VIDEO_CHN1;
static APP_CHN_S g_src, g_dst;

static pthread_t g_vthread, g_ai_thread, g_aenc_thread;
static int g_audio_started;

static int vi_start(const struct source_config *cfg)
{
    RECTANGLE_S res;
    VI_DEV_ATTR dev;
    VI_CHN_ATTR_EX main_attr, sub_attr;
    int sub_w, sub_h;

    if (ak_vi_open(VIDEO_DEV0) != AK_SUCCESS) {
        LOG_E("ak_vi_open failed");
        return -1;
    }
    if (ak_vi_load_sensor_cfg(VIDEO_DEV0, cfg->isp_conf) != AK_SUCCESS) {
        LOG_E("cannot load ISP config %s", cfg->isp_conf);
        goto fail;
    }
    if (ak_vi_get_sensor_resolution(VIDEO_DEV0, &res) != AK_SUCCESS) {
        LOG_E("cannot read sensor resolution");
        goto fail;
    }
    LOG_I("sensor resolution %dx%d", res.width, res.height);

    /* Main channel always runs at sensor resolution; the sub channel
     * gets the requested WebRTC size when vi_chn == 1. */
    sub_w = (cfg->vi_chn == 1) ? cfg->width : 640;
    sub_h = (cfg->vi_chn == 1) ? cfg->height : 360;

    memset(&dev, 0, sizeof(dev));
    dev.dev_id = VIDEO_DEV0;
    dev.crop.width = res.width;
    dev.crop.height = res.height;
    dev.max_width = res.width;
    dev.max_height = res.height;
    dev.sub_max_width = sub_w;
    dev.sub_max_height = sub_h;
    dev.frame_rate = cfg->fps;
    if (ak_vi_set_dev_attr(VIDEO_DEV0, &dev) != AK_SUCCESS) {
        LOG_E("ak_vi_set_dev_attr failed");
        goto fail;
    }

    memset(&main_attr, 0, sizeof(main_attr));
    main_attr.chn_id = VIDEO_CHN0;
    main_attr.res.width = (cfg->vi_chn == 0) ? cfg->width : res.width;
    main_attr.res.height = (cfg->vi_chn == 0) ? cfg->height : res.height;
    main_attr.frame_depth = FRAME_DEPTH;
    main_attr.frame_rate = cfg->fps;
    main_attr.mode = FRAME_MODE;
    if (ak_vi_set_chn_attr_ex(VIDEO_CHN0, &main_attr) != AK_SUCCESS) {
        LOG_E("cannot set VI main channel attributes");
        goto fail;
    }

    memset(&sub_attr, 0, sizeof(sub_attr));
    sub_attr.chn_id = VIDEO_CHN1;
    sub_attr.res.width = sub_w;
    sub_attr.res.height = sub_h;
    sub_attr.frame_depth = FRAME_DEPTH;
    sub_attr.frame_rate = cfg->fps;
    if (ak_vi_set_chn_attr_ex(VIDEO_CHN1, &sub_attr) != AK_SUCCESS) {
        LOG_E("cannot set VI sub channel attributes");
        goto fail;
    }

    if (ak_vi_enable_dev(VIDEO_DEV0) || ak_vi_enable_chn(VIDEO_CHN0) ||
        ak_vi_enable_chn(VIDEO_CHN1)) {
        LOG_E("cannot enable VI");
        goto fail;
    }
    return 0;

fail:
    ak_vi_close(VIDEO_DEV0);
    return -1;
}

static void vi_stop(void)
{
    ak_vi_disable_chn(VIDEO_CHN0);
    ak_vi_disable_chn(VIDEO_CHN1);
    ak_vi_disable_dev(VIDEO_DEV0);
    ak_vi_close(VIDEO_DEV0);
}

static int venc_start(const struct source_config *cfg)
{
    struct venc_param p;
    struct venc_rc_param rc;

    memset(&p, 0, sizeof(p));
    memset(&rc, 0, sizeof(rc));
    p.width = cfg->width;
    p.height = cfg->height;
    p.fps = cfg->fps;
    p.goplen = cfg->fps * 2;          /* key frame every 2 s */
    p.target_kbps = cfg->kbps;
    p.max_kbps = cfg->kbps + cfg->kbps / 2;
    /* Constrained baseline is what every browser decodes in hardware. */
    p.profile = PROFILE_BASE;
    p.br_mode = BR_MODE_CBR;
    p.minqp = 25;
    p.maxqp = 48;
    p.initqp = (p.minqp + p.maxqp) / 2;
    p.chroma_mode = CHROMA_4_2_0;
    p.jpeg_qlevel = JPEG_QLEVEL_DEFAULT;
    p.enc_out_type = H264_ENC_TYPE;
    p.enc_level = 30;
    p.smart_mode = 0;
    rc.enable_MMA = 1;

    if (ak_venc_open_ex(&p, &rc, &g_venc) != AK_SUCCESS || g_venc < 0) {
        LOG_E("ak_venc_open_ex failed");
        g_venc = -1;
        return -1;
    }

    g_src.mid = MODULE_ID_VI;
    g_src.oid = g_vi_chn;
    g_dst.mid = MODULE_ID_VENC;
    g_dst.oid = g_venc;

    APP_BIND_PARAM bind;
    memset(&bind, 0, sizeof(bind));
    bind.frame_rate = cfg->fps;
    bind.frame_depth = 4;
    bind.mode = USER_ENCODE_MODE;
    if (ak_app_video_bind_chn(&g_src, &g_dst, &bind) != AK_SUCCESS) {
        LOG_E("ak_app_video_bind_chn failed");
        ak_venc_close(g_venc);
        g_venc = -1;
        return -1;
    }
    ak_app_video_set_dst_chn_active(&g_dst, AK_TRUE);
    ak_venc_request_idr(g_venc);
    return 0;
}

static void *video_thread(void *arg)
{
    (void)arg;
    struct video_stream stream;

    ak_thread_set_name("akwebrtc_venc");
    while (g_run) {
        memset(&stream, 0, sizeof(stream));
        if (ak_app_video_venc_get_stream(&g_dst, &stream) != AK_SUCCESS) {
            ak_sleep_ms(10);
            continue;
        }
        if (stream.len > 0)
            g_vcb(stream.data, stream.len,
                  stream.frame_type != FRAME_TYPE_P || h264_is_key(stream.data, stream.len),
                  g_user);
        ak_venc_release_stream(g_venc, &stream);
    }
    return NULL;
}

/* ---- audio: AI (PCM 16 bit) -> AENC (A-law) ---- */

static int audio_start(void)
{
    struct ak_audio_in_param in;
    struct aenc_param enc;

    memset(&in, 0, sizeof(in));
    in.pcm_data_attr.sample_rate = PCMA_RATE;
    in.pcm_data_attr.sample_bits = AK_AUDIO_SMPLE_BIT_16;
    in.pcm_data_attr.channel_num = AUDIO_CHANNEL_MONO;
    in.dev_id = DEV_ADC;
    if (ak_ai_open(&in, &g_ai)) {
        LOG_E("ak_ai_open failed");
        g_ai = -1;
        return -1;
    }
    ak_ai_set_source(g_ai, AI_SOURCE_MIC);
    ak_ai_set_gain(g_ai, 5);
    /* 20 ms frames = 160 samples * 2 bytes; one PCMA RTP packet each. */
    ak_ai_set_frame_length(g_ai, 320);

    memset(&enc, 0, sizeof(enc));
    enc.type = AK_AUDIO_TYPE_PCM_ALAW;
    enc.aenc_data_attr.sample_rate = PCMA_RATE;
    enc.aenc_data_attr.sample_bits = AK_AUDIO_SMPLE_BIT_16;
    enc.aenc_data_attr.channel_num = AUDIO_CHANNEL_MONO;
    if (ak_aenc_open(&enc, &g_aenc)) {
        LOG_E("ak_aenc_open failed");
        ak_ai_close(g_ai);
        g_ai = g_aenc = -1;
        return -1;
    }
    return 0;
}

static void *ai_thread(void *arg)
{
    (void)arg;
    struct frame f;

    ak_thread_set_name("akwebrtc_ai");
    ak_ai_start_capture(g_ai);
    while (g_run) {
        memset(&f, 0, sizeof(f));
        if (ak_ai_get_frame(g_ai, &f, 0)) {
            ak_sleep_ms(5);
            continue;
        }
        ak_aenc_send_frame(g_aenc, &f, 0);
        ak_ai_release_frame(g_ai, &f);
    }
    ak_ai_stop_capture(g_ai);
    return NULL;
}

static void *aenc_thread(void *arg)
{
    (void)arg;
    struct audio_stream s;

    ak_thread_set_name("akwebrtc_aenc");
    while (g_run) {
        memset(&s, 0, sizeof(s));
        if (ak_aenc_get_stream(g_aenc, &s, 0)) {
            ak_sleep_ms(5);
            continue;
        }
        if (s.data && s.len)
            g_acb(s.data, s.len, g_user);
        ak_aenc_release_stream(g_aenc, &s);
    }
    return NULL;
}

int source_start(const struct source_config *cfg, source_video_cb vcb,
                 source_audio_cb acb, void *user)
{
    sdk_run_config sdk;

    g_vcb = vcb;
    g_acb = acb;
    g_user = user;
    g_vi_chn = (cfg->vi_chn == 0) ? VIDEO_CHN0 : VIDEO_CHN1;

    memset(&sdk, 0, sizeof(sdk));
    sdk.mem_trace_flag = SDK_RUN_NORMAL;
    if (ak_sdk_init(&sdk) != AK_SUCCESS) {
        LOG_E("ak_sdk_init failed");
        return -1;
    }
    if (vi_start(cfg) != 0)
        goto fail_sdk;
    if (venc_start(cfg) != 0)
        goto fail_vi;

    g_run = 1;
    if (pthread_create(&g_vthread, NULL, video_thread, NULL) != 0)
        goto fail_venc;

    if (cfg->audio && acb && audio_start() == 0) {
        pthread_create(&g_ai_thread, NULL, ai_thread, NULL);
        pthread_create(&g_aenc_thread, NULL, aenc_thread, NULL);
        g_audio_started = 1;
    }
    LOG_I("anyka source: VI chn%d %dx%d@%d H.264 %d kbps, audio %s",
          cfg->vi_chn, cfg->width, cfg->height, cfg->fps, cfg->kbps,
          g_audio_started ? "PCMA" : "off");
    return 0;

fail_venc:
    g_run = 0;
    ak_app_video_unbind_chn(&g_src, &g_dst);
    ak_venc_close(g_venc);
fail_vi:
    vi_stop();
fail_sdk:
    ak_sdk_exit();
    return -1;
}

void source_request_idr(void)
{
    if (g_venc >= 0)
        ak_venc_request_idr(g_venc);
}

void source_stop(void)
{
    if (!g_run)
        return;
    g_run = 0;
    pthread_join(g_vthread, NULL);
    if (g_audio_started) {
        pthread_join(g_ai_thread, NULL);
        pthread_join(g_aenc_thread, NULL);
        ak_aenc_close(g_aenc);
        ak_ai_close(g_ai);
    }
    ak_app_video_set_dst_chn_active(&g_dst, AK_FALSE);
    ak_app_video_unbind_chn(&g_src, &g_dst);
    ak_venc_close(g_venc);
    vi_stop();
    ak_sdk_exit();
}
