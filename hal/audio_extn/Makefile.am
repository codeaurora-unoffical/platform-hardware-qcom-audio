AM_CFLAGS = -I ${WORKSPACE}/external/tinyalsa/include \
            -I ${WORKSPACE}/system/media/audio_effects/include \
            -I ${WORKSPACE}/system/media/audio_utils/include \
            -I ${WORKSPACE}/frameworks/system/media/audio/include \
            -I $(top_srcdir)/hal \
            -I $(top_srcdir)/hal/audio_extn \
            -I $(top_srcdir)/hal/voice_extn \
            -I $(top_srcdir)/hal/${TARGET_PLATFORM}

h_sources = audio_defs.h \
            audio_extn.h \
            ../audio_hw.h \
            ../voice.h

################################## SPKR-PROTECTION LIB ####################################
if SPKR_PROT
AM_CFLAGS += -DSPKR_PROT_ENABLED

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)

lib_LTLIBRARIES = audio.spkr.prot.la
audio_spkr_prot_la_SOURCES = spkr_protection.c
audio_spkr_prot_la_LIBADD = $(GLIB_LIBS) -llog -lcutils -ltinyalsa
audio_spkr_prot_la_LIBADD += -ltinycompress -laudioroute -ldl -lexpat -laudioutils
audio_spkr_prot_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
audio_spkr_prot_la_CFLAGS += -Dstrlcat=g_strlcat
audio_spkr_prot_la_CFLAGS += -DINT_MAX=0x7fffffff
audio_spkr_prot_la_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
audio_spkr_prot_la_CFLAGS += -DLINUX_ENABLED $(TARGET_CFLAGS) -DAUDIO_EXTN_FORMATS_ENABLED
audio_spkr_prot_la_CFLAGS += -DNDEBUG
audio_spkr_prot_la_LDFLAGS = -module -shared -avoid-version
endif
