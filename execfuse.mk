################################################################################
#
# execfuse buildroot package
#
################################################################################
EXECFUSE_VERSION = 1.0
EXECFUSE_SITE = https://github.com/vi/execfuse 
EXECFUSE_LICENSE = GPL-2.0+
EXECFUSE_SITE = /buildroot-v86/package/execfuse
EXECFUSE_SITE_METHOD = local
EXECFUSE_INSTALL_STAGING = YES
EXECFUSE_DEPENDENCIES = libfuse

# We need to override the C compiler used in the Makefile to 
# use buildroot's cross-compiler instead of cc.  Switch cc to $(CC)
# so we can override the variable via env vars.
define EXECFUSE_MAKEFILE_FIXUP
  echo no fixup
endef

EXECFUSE_PRE_BUILD_HOOKS += EXECFUSE_MAKEFILE_FIXUP

define EXECFUSE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(TARGET_CONFIGURE_OPTS) LDOPTIONS="-lfuse" LIBS="$(TARGET_CFLAGS) -lfuse"
endef

define EXECFUSE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/execfuse $(TARGET_DIR)/usr/bin/execfuse
endef

define EXECFUSE_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0755 $(@D)/execfuse $(STAGING_DIR)/usr/bin/execfuse
endef

$(eval $(generic-package))
