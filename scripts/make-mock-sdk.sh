#!/bin/sh
#
# Build a *mock* AnyCloud39AV100 SDK for CI / build testing only.
#
# Takes the real headers from the public SDK mirror and produces stub
# shared libraries whose functions all fail, so the Buildroot packages
# can be configured, compiled and linked without the vendor's
# closed-source media libraries. The resulting firmware cannot capture
# video. Never flash it.
#
# Usage: make-mock-sdk.sh <public-sdk-checkout> <out-dir> <cross-gcc>
#   e.g. make-mock-sdk.sh ak3918av100 /tmp/mock-sdk arm-linux-gnueabi-gcc
set -eu

SDK="$1"
OUT="$2"
CC="$3"

mkdir -p "$OUT/platform/lib" "$OUT/rootfs/rootfs/etc" "$OUT/rootfs/rootfs/usr/sbin" \
	"$OUT/rootfs/ko"
cp -a "$SDK/platform/include" "$OUT/platform/"
cp -a "$SDK"/rootfs/rootfs/etc/isp_*.conf "$OUT/rootfs/rootfs/etc/"
cp -a "$SDK"/rootfs/rootfs/usr/sbin/*.sh "$OUT/rootfs/rootfs/usr/sbin/"

# Every SDK call akwebrtc makes, as a stub returning failure (-1).
STUB="$OUT/stub.c"
{
	for f in ak_sdk_init ak_sdk_exit ak_vi_open ak_vi_close ak_vi_load_sensor_cfg \
		ak_vi_get_sensor_resolution ak_vi_set_dev_attr ak_vi_set_chn_attr_ex \
		ak_vi_enable_dev ak_vi_enable_chn ak_vi_disable_chn ak_vi_disable_dev \
		ak_venc_open_ex ak_venc_close ak_venc_request_idr ak_venc_release_stream \
		ak_app_video_bind_chn ak_app_video_unbind_chn ak_app_video_set_dst_chn_active \
		ak_app_video_venc_get_stream ak_ai_open ak_ai_close ak_ai_set_source \
		ak_ai_set_gain ak_ai_set_frame_length ak_ai_start_capture ak_ai_stop_capture \
		ak_ai_get_frame ak_ai_release_frame ak_aenc_open ak_aenc_close \
		ak_aenc_send_frame ak_aenc_get_stream ak_aenc_release_stream \
		ak_thread_set_name; do
		echo "int $f(void) { return -1; }"
	done
	echo "void ak_sleep_ms(int ms) { (void)ms; }"
} > "$STUB"

"$CC" -shared -fPIC -o "$OUT/platform/lib/libplat_common.so" "$STUB"
for l in plat_mem plat_thread plat_log plat_osal plat_dbg plat_timer plat_vi \
	plat_isp_sdk plat_vpss mpi_venc app_video plat_ai mpi_aenc plat_vqe \
	akaudiofilter akaudiocodec akmedia; do
	echo "void mock_$l(void) {}" > "$OUT/empty.c"
	"$CC" -shared -fPIC -o "$OUT/platform/lib/lib$l.so" "$OUT/empty.c"
done
rm -f "$OUT/stub.c" "$OUT/empty.c"
echo "mock SDK in $OUT (stubs only, cannot stream)"
