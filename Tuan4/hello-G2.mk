HELLO_G2_VERSION = 1.0
HELLO_G2_SITE = $(TOPDIR)/package/hello-G2/src
HELLO_G2_SITE_METHOD = local

define HELLO_G2_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(@D)/hello.c -o $(@D)/hello-G2 $(TARGET_LDFLAGS)
endef

define HELLO_G2_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hello-G2 $(TARGET_DIR)/usr/bin/hello-G2
endef

$(eval $(generic-package))
