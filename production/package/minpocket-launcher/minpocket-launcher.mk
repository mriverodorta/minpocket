MINPOCKET_LAUNCHER_VERSION = 0.1
MINPOCKET_LAUNCHER_SITE = $(BR2_EXTERNAL_MINPOCKET_PATH)/../native
MINPOCKET_LAUNCHER_SITE_METHOD = local
MINPOCKET_LAUNCHER_DEPENDENCIES = libdrm util-linux exfatprogs
MINPOCKET_LAUNCHER_LICENSE = GPL-2.0-only, CC0-1.0 (Monogram font), BSD-2-Clause (LZ4), BSD-3-Clause (near-lossless preprocessing)
MINPOCKET_LAUNCHER_LICENSE_FILES = vendor/lz4/LICENSE vendor/near-lossless/COPYING vendor/near-lossless/PATENTS

define MINPOCKET_LAUNCHER_BUILD_CMDS
	python3 $(BR2_EXTERNAL_MINPOCKET_PATH)/../design/compile-navigation.py --output $(@D)/navigation-default.h
	python3 $(BR2_EXTERNAL_MINPOCKET_PATH)/../design/compile-artwork-fade.py --output $(@D)/artwork-fade.h
	python3 $(BR2_EXTERNAL_MINPOCKET_PATH)/../design/compile-home.py --output $(@D)/home-layout-default.c
	python3 $(BR2_EXTERNAL_MINPOCKET_PATH)/../design/compile-theme.py \
		--output $(@D)/theme-default.c
	$(TARGET_CC) $(TARGET_CFLAGS) -pthread -Wall -Wextra -Werror \
		-I$(@D) -I$(STAGING_DIR)/usr/include/libdrm \
		$(@D)/launcher.c $(@D)/runtime-home.c $(@D)/vendor/lz4/lz4.c $(@D)/vendor/lz4/lz4hc.c $(@D)/asset-cache.c $(@D)/asset-codec.c $(@D)/vendor/near-lossless/near-lossless.c $(@D)/asset-platform.c $(@D)/home.c $(@D)/home-view.c $(@D)/scene.c $(@D)/game-art.c $(@D)/home-layout-default.c $(@D)/display.c $(@D)/dev.c $(@D)/battery.c $(@D)/font.c \
		$(@D)/wifi.c $(@D)/wifi-linux.c $(@D)/ui.c $(@D)/style.c $(@D)/status-bar.c $(@D)/theme-default.c \
		$(@D)/indicators/battery.c $(@D)/indicators/wifi.c $(@D)/indicators/clock.c $(@D)/indicators/inspector.c \
		-o $(@D)/minpocket-launcher $(TARGET_LDFLAGS) -ldrm
	$(TARGET_CC) $(TARGET_CFLAGS) -pthread -Wall -Wextra -Werror -DMP_LIBRARY_TOOL -I$(@D) \
		$(@D)/runtime-home.c $(@D)/vendor/lz4/lz4.c $(@D)/vendor/lz4/lz4hc.c $(@D)/asset-platform.c $(@D)/asset-cache.c $(@D)/asset-codec.c $(@D)/vendor/near-lossless/near-lossless.c \
		$(@D)/home-view.c $(@D)/home.c $(@D)/home-layout-default.c $(@D)/game-art.c \
		$(@D)/scene.c $(@D)/font.c $(@D)/ui.c $(@D)/style.c $(@D)/theme-default.c \
		-o $(@D)/minpocket-library $(TARGET_LDFLAGS)
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror \
		$(@D)/data.c -o $(@D)/minpocket-data $(TARGET_LDFLAGS) -lfdisk
endef

define MINPOCKET_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/minpocket-launcher $(TARGET_DIR)/sbin/minpocket-launcher
	$(INSTALL) -D -m 0755 $(@D)/minpocket-library $(TARGET_DIR)/sbin/minpocket-library
	$(INSTALL) -D -m 0755 $(@D)/minpocket-data $(TARGET_DIR)/sbin/minpocket-data
	$(INSTALL) -D -m 0644 $(@D)/vendor/near-lossless/COPYING $(TARGET_DIR)/usr/share/licenses/minpocket-near-lossless/COPYING
	$(INSTALL) -D -m 0644 $(@D)/vendor/near-lossless/PATENTS $(TARGET_DIR)/usr/share/licenses/minpocket-near-lossless/PATENTS
	ln -sf sbin/minpocket-launcher $(TARGET_DIR)/init
	rm -f $(TARGET_DIR)/etc/localtime $(TARGET_DIR)/etc/timezone

endef

$(eval $(generic-package))
