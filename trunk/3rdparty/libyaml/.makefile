# This file is used for common development targets that can be done with
# needing the cumbersome bootstrapping process.
#
# You can use it like this:
#
#   make -f .makefile indent
#
# If you copy or link this file to `GNUmakefile` then you can just do:
#
#   make indent
#
# When copied to `GNUmakefile`, this file is can also be used for bootstrapping
# Makefile targets. Since GNUmakefile is loaded before Makefile, we do the
# bootstrapping tasks need to get a Makefile first, then we use the Makefile to
# make our target.

# Remind user when they are using GNUmakefile:
ifeq ($(lastword $(MAKEFILE_LIST)),GNUmakefile)
    $(info *** NOTE: GNUmakefile in use. ***)
endif

MAKE_TARGETS := \
	all \
	all-am \
	all-recursive \
	docker-build \
	docker-dist \
	install \
	test \
	test-all \
	test-suite \

# SOURCE_FILES := $(shell find . | grep '\.c$$')
SOURCE_FILES := $(shell find tests/run-test-suite | grep '\.c$$')
ifneq ($(shell which gindent),)
INDENT := gindent
else
INDENT := indent
endif

#
# Proxy make targets:
#
default: all

# Proxy these targets to the real Makefile, after bootstrapping is necessary.
$(MAKE_TARGETS): Makefile
	@make -f $< $@

Makefile: Makefile.in
	./configure

Makefile.in:
	./bootstrap

#
# Development make targets:
#
indent:
	$(INDENT) $(SOURCE_FILES)

distclean purge:
	git clean -dxf -e GNUmakefile
	rm -fr tests/run-test-suite
	git worktree prune
