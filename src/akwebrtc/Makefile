# akwebrtc
#
#   make SOURCE=ak   ANYKA_INC=... ANYKA_LIB=...   (camera build, default)
#   make SOURCE=file                               (host test build)
#
# PEER_CFLAGS / PEER_LIBS point at libpeer (staging dir under Buildroot).

SOURCE      ?= ak
CC          ?= gcc
CFLAGS      ?= -O2
# Internal flags go in AK_CFLAGS so a CFLAGS=... on the command line
# (as Buildroot passes it) does not drop them.
AK_CFLAGS    = -std=gnu99 -Wall -Wextra -Wno-unused-parameter
PEER_CFLAGS ?=
PEER_LIBS   ?= -lpeer
LIBPEER_VIDEO_FPS ?=

OBJS = main.o http.o session.o index_html.o

ifeq ($(SOURCE),file)
OBJS   += source_file.o
AK_CFLAGS += -DAKWEBRTC_FILE_SOURCE
LDLIBS  = $(PEER_LIBS) -lpthread
else
# Same library set as the SDK's kd_video sample, minus RTSP/OSD/mux/record.
ANYKA_INC ?= $(ANYKA_SDK)/platform/include
ANYKA_LIB ?= $(ANYKA_SDK)/platform/lib
ANYKA_LIBS = plat_common plat_mem plat_thread plat_log plat_osal plat_dbg \
             plat_timer plat_vi plat_isp_sdk plat_vpss mpi_venc app_video \
             plat_ai mpi_aenc plat_vqe akaudiofilter akaudiocodec akmedia
OBJS   += source_ak.o
AK_CFLAGS += -D__CHIP_AK3918AV100_SERIES -I$(ANYKA_INC)
LDLIBS  = $(PEER_LIBS) -L$(ANYKA_LIB) -Wl,--allow-shlib-undefined \
          -Wl,--start-group $(addprefix -l,$(ANYKA_LIBS)) -Wl,--end-group -lpthread
endif

ifneq ($(LIBPEER_VIDEO_FPS),)
AK_CFLAGS += -DLIBPEER_VIDEO_FPS=$(LIBPEER_VIDEO_FPS)
endif

all: akwebrtc

akwebrtc: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c *.h
	$(CC) $(CFLAGS) $(AK_CFLAGS) $(PEER_CFLAGS) -c -o $@ $<

install: akwebrtc
	install -D -m 0755 akwebrtc $(DESTDIR)/usr/bin/akwebrtc

clean:
	rm -f *.o akwebrtc

.PHONY: all install clean
