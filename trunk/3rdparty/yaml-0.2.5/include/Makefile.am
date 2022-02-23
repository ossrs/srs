AM_CPPFLAGS = yaml.h
DOXYGEN_CFG = $(top_srcdir)/doc/doxygen.cfg

nobase_include_HEADERS = $(AM_CPPFLAGS)

if DOXYGEN

html: $(AM_CPPFLAGS) $(DOXYGEN_CFG)
	PACKAGE=$(PACKAGE) VERSION=$(VERSION) top_srcdir=$(top_srcdir) top_builddir=$(top_builddir) doxygen $(DOXYGEN_CFG)

endif

maintainer-clean-local:
	-rm -rf $(top_builddir)/doc/html

dist-hook: html
	cp -a $(top_builddir)/doc/html $(top_distdir)/doc
