ACLOCAL_AMFLAGS = -I build/autotools

V         = @
Q         = $(V:1=)
QUIET_GEN = $(Q:@=@echo '  GEN   '$@;)

SUBDIRS =		\
	build		\
	src

80appmenu: 80appmenu.in
	sed -e "s|\@libdir\@|$(libdir)|" $< > $@

xsessiondir = $(sysconfdir)/X11/Xsession.d

nodist_xsession_DATA = 80appmenu

EXTRA_DIST = $(xsession_DATA) \
             configure.ac \
             Makefile.am \
             autogen.sh \
			 80appmenu.in

DISTCLEANFILES = 80appmenu
