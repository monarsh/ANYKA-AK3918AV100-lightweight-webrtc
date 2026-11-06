# Lightweight WebRTC for Anyka AK3918AV100

A Buildroot external tree (`BR2_EXTERNAL`) that builds a small camera
userspace for the **Anyka AK3918AV100** (AnyCloud39AV100, ARM926EJ-S). It
streams the SoC's hardware H.264 encoder to web browsers over WebRTC using
[libpeer](https://github.com/sepfy/libpeer). It does not use an RTSP server,
a media gateway or a cloud relay.

```
sensor ─► ISP/VI ─► VENC (H.264) ─┐
mic ───► AI ─────► AENC (G.711A) ─┴► akwebrtc ─► libpeer (ICE · DTLS-SRTP · RTP) ─► browser
                                       │
                                       └─ HTTP :8080  player page + SDP signalling
```

## Status

| Part | State |
|---|---|
| `akwebrtc` WebRTC path (signalling, libpeer, H.264 RTP, viewer handling) | **Tested on a PC**. A file source played in headless Chromium at 640×360, 20 fps. Dead viewers are dropped by the STUN keepalive. |
| Buildroot packaging (`libpeer`, `akwebrtc`, `anyka-av100-sdk`) | **Build-tested** with Buildroot 2025.02.9 (`make akwebrtc`) using a generic ARMv5 soft-float toolchain and a mock SDK (`scripts/build-test.sh`). The output is an ARM EABI5 `akwebrtc` linked against the cross-built `libpeer.so`, plus the init scripts and ISP links. A full image build was not run because the source mirrors were unreachable from the test machine. |
| Anyka capture code (`source_ak.c`) | Written from the SDK's `kd_video` sample and **type-checked against the real SDK headers**. **Not yet run on hardware.** |
| Vendor toolchain settings in the defconfig | Taken from the SDK's `rules.mk` (gcc 5.5, uClibc, kernel 4.4). **Not tried with the real toolchain.** |

## What you need

1. **The full vendor SDK, `AnyCloud39AV100_SDK_V1.12`**, from your board or
   module supplier. The public mirror at
   [KD0000001/ak3918av100](https://github.com/KD0000001/ak3918av100) has the
   headers, samples, kernel and ISP configs. It does **not** include the
   prebuilt media libraries (`libplat_*.so`, `libmpi_venc.so`,
   `libapp_video.so`, …) or the kernel modules, and you need both.
2. **The SDK toolchain** `arm-anycloud-linux-uclibcgnueabi` (gcc 5.5.0),
   installed in `/opt/arm-anycloud-linux-uclibcgnueabi`, which is where the
   SDK's `rules.mk` expects it.
3. Buildroot. 2025.02.x LTS is the version used for testing.

The kernel and U-Boot are **not** built by Buildroot. The media drivers are
prebuilt modules tied to the SDK kernel (4.4.282), so build those with the
SDK's own `./auto_build.sh` and use this tree for the root filesystem.

## Build

```sh
git clone -b 2025.02.9 https://gitlab.com/buildroot.org/buildroot.git
git clone <this repo> akwebrtc
# put the vendor SDK next to it, or set the path in menuconfig:
#   ../AnyCloud39AV100_SDK_V1.12

cd buildroot
make BR2_EXTERNAL=../akwebrtc anyka_ak3918av100_webrtc_defconfig
make menuconfig     # External options → Anyka AK3918AV100 WebRTC
make
```

The output is `output/images/rootfs.squashfs`. Flashing depends on the board.
The vendor layout on NOR is a squashfs root, a squashfs `/usr` (APP) and a
jffs2 `/etc/config`. This build makes one root filesystem, so it must fit the
root partition, or you must change the partition table in your board's DTS.
Use the SDK's burn tool or U-Boot to write it.

### Settings (`make menuconfig` → *External options*)

| Option | Default | Notes |
|---|---|---|
| `ANYKA_AV100_SDK_PATH` | `../AnyCloud39AV100_SDK_V1.12` | Top of the vendor SDK |
| `ANYKA_AV100_SDK_LIB_DIR` | `platform/lib` | Where the `.so` media libraries are |
| `ANYKA_AV100_SDK_SENSOR` | `gc4653` | Runs `/usr/sbin/<sensor>.sh install` at boot (loads `ak_isp.ko` and the sensor driver) |
| `ANYKA_AV100_SDK_ISP_CONF` | `isp_gc4653_mipi_2lane_av100_hp.conf` | Linked to `/etc/isp/isp.conf` |
| `LIBPEER_VIDEO_FPS` | `15` | Must equal `akwebrtc -f` |
| `LIBPEER_KEEPALIVE_MS` | `5000` | Detects closed browser tabs |
| `LIBPEER_USRSCTP` | off | Off uses libpeer's built-in minimal SCTP |
| `AKWEBRTC_ARGS` | `-c 1 -W 640 -H 360 -f 15 -b 512 -m 2` | Written to `/etc/default/akwebrtc` |

## Use

Open `http://<camera-ip>:8080/` in a browser. The page connects on load.

```
akwebrtc [options]
  -s SOURCE   'ak' (camera) or 'file:/path/stream.h264' (host build)
  -i FILE     ISP config (default /etc/isp/isp.conf)
  -c CHN      VI channel: 0 main (sensor res), 1 sub (default)
  -W/-H       encode size (default 640x360)
  -f FPS      frame rate (default 15)
  -b KBPS     bitrate (default 512)
  -a          microphone audio, G.711 A-law
  -p PORT     HTTP port (default 8080)
  -m N        max viewers (default 2)
  -S URL      STUN server for viewers outside the LAN
```

### Signalling API

libpeer only supports the camera making the SDP offer, so the order is the
reverse of WHEP:

| Request | Result |
|---|---|
| `POST /api/offer` | `201`, body is the SDP offer, `Location: /api/session/<id>` |
| `POST /api/session/<id>` (body: SDP answer) | `204` |
| `DELETE /api/session/<id>` | `204` |
| `GET /api/status` | `{"viewers":N}` |

The browser must send its answer with all ICE candidates (no trickle). The
built-in page waits for ICE gathering to finish before it posts.

## Sizing notes for the ARM926EJ-S

- The DTLS handshake (RSA-1024 in mbedTLS) takes a noticeable moment on
  this CPU. After that, the per-viewer cost is SRTP (AES-128-CM +
  HMAC-SHA1) on every packet. That cost grows with bitrate × viewers, so
  the defaults use the sub channel (640×360, 512 kbps) and allow 2 viewers.
- Encoding uses H.264 Baseline profile, which every browser can decode in
  hardware.
- Flash use: `akwebrtc` is about 35 KB. `libpeer.so` with mbedTLS,
  libsrtp and cJSON linked in is about 950 KB stripped at `-O2` (the
  defconfig uses `-Os`). Most of that is mbedTLS's default config, so
  trimming that config is the obvious next step for 8 MB boards.

## Security

There is no authentication or TLS on the HTTP port. Media is encrypted
(DTLS-SRTP), but anyone who can reach port 8080 can watch. Keep it on a
trusted LAN, or put it behind a VPN or reverse proxy. Answer SDPs with lines
of 256 bytes or more are rejected, because libpeer parses lines into a
256-byte buffer.

## Layout

```
external.desc, external.mk, Config.in     BR2_EXTERNAL glue
configs/anyka_ak3918av100_webrtc_defconfig
board/anyka/ak3918av100/post-build.sh     /etc/config on tmpfs
package/libpeer/                          libpeer @ 5b849de + build-option patch
package/anyka-av100-sdk/                  copies libs/.ko/ISP files from your SDK
package/akwebrtc/                         the streamer + init script
src/akwebrtc/                             streamer source (C, ~1000 lines)
scripts/build-test.sh                     packaging test without the vendor SDK
scripts/make-mock-sdk.sh                  stub SDK for that test (cannot stream)
```

## Testing on a PC

```sh
# libpeer (with this tree's patch applied), then:
make -C src/akwebrtc SOURCE=file PEER_CFLAGS=-I<prefix>/include \
     PEER_LIBS="-L<prefix>/lib -lpeer" LIBPEER_VIDEO_FPS=20
ffmpeg -f lavfi -i testsrc=size=640x360:rate=20 -t 10 \
       -c:v libx264 -profile:v baseline -g 40 -bf 0 test.h264
src/akwebrtc/akwebrtc -s file:test.h264 -f 20
# open http://localhost:8080/
```

## Licences

The `akwebrtc` source and the packaging are under the MIT licence. libpeer
is MIT. The Anyka SDK is proprietary: this tree never redistributes it and
only copies files from your local copy into your image.
