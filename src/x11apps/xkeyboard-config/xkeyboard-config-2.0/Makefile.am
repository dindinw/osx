AUTOMAKE_OPTIONS = foreign

SUBDIRS = compat geometry keycodes keymap rules semantics symbols types docs

pkgconfigdir = $(datadir)/pkgconfig
pkgconfig_DATA = xkeyboard-config.pc

EXTRA_DIST=config.rpath COPYING CREDITS README \
       autogen.sh \
       xkeyboard-config.pc.in \
       xkeyboard-config.spec \
       xkeyboard-config.spec.in \
       intltool-extract.in intltool-merge.in intltool-update.in

DISTCLEANFILES=intltool-extract intltool-merge intltool-update

# ACLOCAL_AMFLAGS = -I m4

sync-po:
	rsync -Lrtvz translationproject.org::tp/latest/xkeyboard-config/  po
