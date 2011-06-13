CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =
EXTRA_DIST=

moduledir = $(libdir)/gtk-2.0/2.10.0/menuproxies
module_LTLIBRARIES = libappmenu.la

libappmenu_la_SOURCES =		\
	bridge.c \
	bridge.h \
	gen-application-menu-registrar.xml.c \
	gen-application-menu-registrar.xml.h

libappmenu_la_CPPFLAGS =	\
	-Wall -Werror		\
	$(APPMENU_CFLAGS)

libappmenu_la_LDFLAGS =		\
	-module -avoid-version

libappmenu_la_LIBADD =		\
	$(APPMENU_LIBS)

DBUS_SPECS =			\
	application-menu-registrar.xml

INCLUDES =				\
	-I$(srcdir)			\
	-I$(top_srcdir)			\
	-DG_DISABLE_DEPRECATED		\
	-DGDK_PIXBUF_DISABLE_DEPRECATED	\
	-DGDK_DISABLE_DEPRECATED	\
	-DGTK_DISABLE_DEPRECATED

AM_CPPFLAGS =				\
	$(GCC_FLAGS)			\
	$(MAINTAINER_CFLAGS)

gen-%.xml.h: %.xml
	@echo "Building $@ from $<"
	@echo "extern const char * _$(subst -,_,$(subst .,_,$(basename $<)));" > $@

gen-%.xml.c: %.xml
	@echo "Building $@ from $<"
	@echo "const char * _$(subst -,_,$(subst .,_,$(basename $<))) = " > $@
	@sed -e "s:\":\\\\\":g" -e s:^:\": -e s:\$$:\\\\n\": $< >> $@
	@echo ";" >> $@

BUILT_SOURCES += \
	gen-application-menu-registrar.xml.h \
	gen-application-menu-registrar.xml.c

CLEANFILES += $(BUILT_SOURCES)

EXTRA_DIST += $(DBUS_SPECS)
