noinst_LTLIBRARIES = libxkb.la libxkbstubs.la

AM_CFLAGS = $(DIX_CFLAGS) \
	-DHAVE_XKB_CONFIG_H

DDX_SRCS = \
        ddxBeep.c \
        ddxCtrls.c \
        ddxFakeMtn.c \
        ddxInit.c \
        ddxKeyClick.c \
        ddxLEDs.c \
        ddxLoad.c \
        ddxList.c \
        ddxDevBtn.c

DIX_SRCS = \
        xkb.c \
        xkbUtils.c \
        xkbEvents.c \
        xkbAccessX.c \
        xkbSwap.c \
        xkbLEDs.c \
        xkbInit.c \
        xkbActions.c \
        xkbPrKeyEv.c

# this should be replaced by a common library or something, ideally -d
XKBFILE_SRCS = \
        maprules.c \
        xkmread.c \
        xkbtext.c \
        xkbfmisc.c \
        xkbout.c

X11_SRCS = \
        XKBMisc.c \
        XKBAlloc.c \
        XKBGAlloc.c \
        XKBMAlloc.c

# ends up unused...
# XI_SRCS = xkbPrOtherEv.c

libxkb_la_SOURCES = $(DDX_SRCS) $(DIX_SRCS) $(XI_SRCS) $(XKBFILE_SRCS) \
                    $(X11_SRCS)
libxkbstubs_la_SOURCES = ddxVT.c ddxPrivate.c ddxKillSrv.c

EXTRA_DIST = xkbDflts.h xkbgeom.h xkb.h

xkbcompileddir = $(XKB_COMPILED_DIR)
dist_xkbcompiled_DATA = README.compiled
