bin_PROGRAMS = amrnbtest

amrnb_CPPFLAGS = -Wno-sign-compare -Werror
amrnb_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
amrnb_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel

amrnbtest_INCLUDES = -I$(WORKSPACE)/system/core/libion/include/

amrnbtest_SOURCES = amrnbtest.c ion_alloc.c

amrnbtest_LDFLAGS   = -lpthread -lm -shared -lutils -lion

amrnbtest_CFLAGS = $(amrnb_CPPFLAGS) $(amrnbtest_INCLUDES)

bin_PROGRAMS += amrwbtest

amrwb_CPPFLAGS = -Wno-sign-compare -Werror
amrwb_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
amrwb_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel

amrwbtest_INCLUDES = -I$(WORKSPACE)/system/core/libion/include/


amrwbtest_SOURCES = amrwbtest.c ion_alloc.c

amrwbtest_LDFLAGS   = -lpthread -lm -shared -lutils -lion

amrwbtest_CFLAGS = $(amrwb_CPPFLAGS) $(amrwbtest_INCLUDES)


bin_PROGRAMS += amrwbplustest

amrwbplus_CPPFLAGS = -Wno-sign-compare -Werror
amrwbplus_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
amrwbplus_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel

amrwbplustest_INCLUDES = -I$(WORKSPACE)/system/core/libion/include/

amrwbplustest_SOURCES = amrwbplustest.c ion_alloc.c

amrwbplustest_LDFLAGS   = -lpthread -lm -shared -lutils -lion

amrwbplustest_CFLAGS = $(amrwbplus_CPPFLAGS) $(amrwbplustest_INCLUDES)


