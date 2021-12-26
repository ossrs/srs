# ===========================================================================
#   http://www.gnu.org/software/autoconf-archive/ax_generate_changelog.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_GENERATE_CHANGELOG()
#
# DESCRIPTION
#
#   Builds a rule for generating a ChangeLog file from version control
#   system commit messages.  Currently, the only supported VCS is git, but
#   support for others could be added in future.
#
#   Defines GENERATE_CHANGELOG_RULES which should be substituted in your
#   Makefile.
#
#   Usage example:
#
#   configure.ac:
#
#     AX_GENERATE_CHANGELOG
#
#   Makefile.am:
#
#     @GENERATE_CHANGELOG_RULES@
#     CHANGELOG_START = 0.2.3^
#     dist-hook: dist-ChangeLog
#
#   ChangeLog (stub committed to VCS):
#
#     The ChangeLog is auto-generated when releasing.
#     If you are seeing this, use 'git log' for a detailed list of changes.
#
#   This results in a "dist-ChangeLog" rule being added to the Makefile.
#   When run, "dist-ChangeLog" will generate a ChangeLog in the
#   $(top_distdir), using $(CHANGELOG_GIT_FLAGS) to format the output from
#   "git log" being run in $(CHANGELOG_GIT_DIR).
#
#   Unless Automake is initialised with the 'foreign' option, a dummy
#   ChangeLog file must be committed to VCS in $(top_srcdir), containing the
#   text above (for example).  It will be substituted by the automatically
#   generated ChangeLog during "make dist".
#
# LICENSE
#
#   Copyright (c) 2015 David King <amigadave@amigadave.com>
#   Copyright (c) 2015 Philip Withnall <philip.withnall@collabora.co.uk>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 1

AC_DEFUN([AX_GENERATE_CHANGELOG],[
	# Find git, defaulting to the 'missing' script so the user gets a nice
	# message if git is missing, rather than a plain 'command not found'.
	AC_PATH_PROG([GIT],[git],[${am_missing_run}git])
	AC_SUBST([GIT])

	# Build the ChangeLog rules.
	m4_pattern_allow([AM_V_GEN])
GENERATE_CHANGELOG_RULES='
# Generate ChangeLog
#
# Optional:
#  - CHANGELOG_START: git commit ID or tag name to output changelogs from
#    (exclusive). (Default: include all commits)
#  - CHANGELOG_GIT_FLAGS: General flags to pass to git-log when generating the
#    ChangeLog. (Default: various)
#  - CHANGELOG_GIT_DIR: .git directory to use. (Default: $(top_srcdir)/.git)

# git-specific
CHANGELOG_GIT_FLAGS ?= --stat -M -C --name-status --no-color
CHANGELOG_GIT_DIR ?= $(top_srcdir)/.git

ifeq ($(CHANGELOG_START),)
CHANGELOG_GIT_RANGE =
else
CHANGELOG_GIT_RANGE = $(CHANGELOG_START)..
endif

# Generate a ChangeLog in $(top_distdir)
dist-ChangeLog:
	$(AM_V_GEN)if $(GIT) \
		--git-dir=$(CHANGELOG_GIT_DIR) --work-tree=$(top_srcdir) log \
		$(CHANGELOG_GIT_FLAGS) $(CHANGELOG_GIT_RANGE) \
		| fmt --split-only >.ChangeLog.tmp; \
	then mv -f .ChangeLog.tmp "$(top_distdir)/ChangeLog"; \
	else rm -f .ChangeLog.tmp; exit 1; fi

.PHONY: dist-ChangeLog
'

	AC_SUBST([GENERATE_CHANGELOG_RULES])
	m4_ifdef([_AM_SUBST_NOTMAKE], [_AM_SUBST_NOTMAKE([GENERATE_CHANGELOG_RULES])])
])
