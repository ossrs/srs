AM_CPPFLAGS = -I$(top_srcdir)/include -Wall
lib_LTLIBRARIES = libyaml.la
libyaml_la_SOURCES = yaml_private.h api.c reader.c scanner.c parser.c loader.c writer.c emitter.c dumper.c
libyaml_la_LDFLAGS = -no-undefined -release $(YAML_LT_RELEASE) -version-info $(YAML_LT_CURRENT):$(YAML_LT_REVISION):$(YAML_LT_AGE)
