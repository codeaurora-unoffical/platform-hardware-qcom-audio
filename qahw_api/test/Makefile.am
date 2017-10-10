bin_PROGRAMS = hal_play_test

PLAY_CFLAGS = -Wno-sign-compare -Werror
PLAY_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
PLAY_INCLUDES = -I $(top_srcdir)/qahw_api/inc

hal_play_test_SOURCES = qahw_playback_test.c \
                        qahw_effect_test.c

hal_play_test_LDADD = -lutils ../libqahw.la
AM_CFLAGS = -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/mm-audio/qahw/inc

hal_play_test_CFLAGS = $(PLAY_CFLAGS) $(PLAY_INCLUDES)
hal_play_test_CFLAGS += $(AM_CFLAGS) -DLINUX_ENABLED
bin_PROGRAMS += hal_rec_test

REC_CFLAGS = $(PLAY_CFLAGS) -Dnullptr=NULL
REC_INCLUDES = $(PLAY_INCLUDES)

hal_rec_test_SOURCES = qahw_multi_record_test.c
hal_rec_test_CFLAGS = -Dstrlcat=g_strlcat $(GLIB_CFLAGS) -include glib.h
hal_rec_test_CFLAGS += $(REC_CFLAGS) $(REC_INCLUDES)
hal_rec_test_CFLAGS += $(AM_CFLAGS)
hal_rec_test_LDADD = -lutils ../libqahw.la $(GLIB_LIBS)
