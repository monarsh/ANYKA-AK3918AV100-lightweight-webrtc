################################################################################
#
# akwebrtc
#
################################################################################

AKWEBRTC_VERSION = 0.1
AKWEBRTC_SITE = $(BR2_EXTERNAL_AKWEBRTC_PATH)/src/akwebrtc
AKWEBRTC_SITE_METHOD = local
AKWEBRTC_LICENSE = MIT
AKWEBRTC_DEPENDENCIES = libpeer anyka-av100-sdk

AKWEBRTC_MAKE_OPTS = \
	CC="$(TARGET_CC)" \
	CFLAGS="$(TARGET_CFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	SOURCE=ak \
	ANYKA_INC=$(STAGING_DIR)/usr/include/anyka \
	ANYKA_LIB=$(STAGING_DIR)/usr/lib \
	PEER_CFLAGS="-I$(STAGING_DIR)/usr/include" \
	PEER_LIBS="-lpeer" \
	LIBPEER_VIDEO_FPS=$(BR2_PACKAGE_LIBPEER_VIDEO_FPS)

# "clean" first: a host test build in src/akwebrtc would otherwise be
# rsynced in and its x86 objects linked.
define AKWEBRTC_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) clean
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(AKWEBRTC_MAKE_OPTS)
endef

define AKWEBRTC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/akwebrtc $(TARGET_DIR)/usr/bin/akwebrtc
	mkdir -p $(TARGET_DIR)/etc/default
	echo 'AKWEBRTC_ARGS="$(call qstrip,$(BR2_PACKAGE_AKWEBRTC_ARGS))"' \
		> $(TARGET_DIR)/etc/default/akwebrtc
endef

define AKWEBRTC_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(AKWEBRTC_PKGDIR)/S95akwebrtc \
		$(TARGET_DIR)/etc/init.d/S95akwebrtc
endef

$(eval $(generic-package))
