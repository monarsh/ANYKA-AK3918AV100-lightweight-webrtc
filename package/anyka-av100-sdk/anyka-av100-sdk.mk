################################################################################
#
# anyka-av100-sdk
#
# Nothing is downloaded: files are copied from the user's local SDK tree.
#
################################################################################

ANYKA_AV100_SDK_LICENSE = PROPRIETARY
ANYKA_AV100_SDK_REDISTRIBUTE = NO
ANYKA_AV100_SDK_INSTALL_STAGING = YES

ANYKA_AV100_SDK_TOPDIR = $(call qstrip,$(BR2_PACKAGE_ANYKA_AV100_SDK_PATH))
ANYKA_AV100_SDK_LIBS = $(ANYKA_AV100_SDK_TOPDIR)/$(call qstrip,$(BR2_PACKAGE_ANYKA_AV100_SDK_LIB_DIR))
ANYKA_AV100_SDK_SENSOR = $(call qstrip,$(BR2_PACKAGE_ANYKA_AV100_SDK_SENSOR))
ANYKA_AV100_SDK_ISP_CONF = $(call qstrip,$(BR2_PACKAGE_ANYKA_AV100_SDK_ISP_CONF))

define ANYKA_AV100_SDK_CHECK
	@test -d "$(ANYKA_AV100_SDK_TOPDIR)/platform/include" || { \
		echo "anyka-av100-sdk: no SDK at '$(ANYKA_AV100_SDK_TOPDIR)'"; \
		echo "  set BR2_PACKAGE_ANYKA_AV100_SDK_PATH (make menuconfig)"; \
		exit 1; }
	@ls $(ANYKA_AV100_SDK_LIBS)/libplat_common.* >/dev/null 2>&1 || { \
		echo "anyka-av100-sdk: no media libraries in $(ANYKA_AV100_SDK_LIBS)"; \
		echo "  the GitHub mirror of the SDK omits them; use the full vendor SDK"; \
		exit 1; }
endef
ANYKA_AV100_SDK_PRE_CONFIGURE_HOOKS += ANYKA_AV100_SDK_CHECK

define ANYKA_AV100_SDK_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/include/anyka $(STAGING_DIR)/usr/lib
	cp -a $(ANYKA_AV100_SDK_TOPDIR)/platform/include/. $(STAGING_DIR)/usr/include/anyka/
	cp -a $(ANYKA_AV100_SDK_LIBS)/*.so* $(STAGING_DIR)/usr/lib/
endef

# Kernel modules: the SDK scripts expect them flat in /usr/modules.
define ANYKA_AV100_SDK_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/lib $(TARGET_DIR)/usr/modules \
		$(TARGET_DIR)/usr/sbin $(TARGET_DIR)/etc/isp
	cp -a $(ANYKA_AV100_SDK_LIBS)/*.so* $(TARGET_DIR)/usr/lib/
	find $(ANYKA_AV100_SDK_TOPDIR)/rootfs -name '*.ko' -not -path '*/lib/modules/*' \
		-exec cp -a {} $(TARGET_DIR)/usr/modules/ \;
	cp -a $(ANYKA_AV100_SDK_TOPDIR)/rootfs/rootfs/etc/isp_*.conf $(TARGET_DIR)/etc/
	ln -sf ../$(ANYKA_AV100_SDK_ISP_CONF) $(TARGET_DIR)/etc/isp/isp.conf
	$(INSTALL) -D -m 0755 \
		$(ANYKA_AV100_SDK_TOPDIR)/rootfs/rootfs/usr/sbin/$(ANYKA_AV100_SDK_SENSOR).sh \
		$(TARGET_DIR)/usr/sbin/$(ANYKA_AV100_SDK_SENSOR).sh
	mkdir -p $(TARGET_DIR)/etc/default
	echo 'SENSOR=$(ANYKA_AV100_SDK_SENSOR)' > $(TARGET_DIR)/etc/default/anyka
	$(INSTALL) -D -m 0755 $(ANYKA_AV100_SDK_PKGDIR)/S40anyka \
		$(TARGET_DIR)/etc/init.d/S40anyka
endef

$(eval $(generic-package))
