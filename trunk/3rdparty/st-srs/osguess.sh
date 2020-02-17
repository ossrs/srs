#
# This script can be used to automatically guess target OS.
# It requires the config.guess utility which is a part of GNU Autoconf.
# GNU Autoconf can be downloaded from ftp://ftp.gnu.org/gnu/autoconf/
#
# Use "default" as a make target for automatic builds.
#


# Specify path to the config.guess utility (unless set via environment)
#CONFIG_GUESS_PATH=


if [ x"$CONFIG_GUESS_PATH" = x ]; then
  echo "Error: CONFIG_GUESS_PATH variable is not set"
  exit 1
fi

if [ ! -f "$CONFIG_GUESS_PATH/config.guess" ]; then
  echo "Can't find $CONFIG_GUESS_PATH/config.guess utility. Wrong path?"
  exit 1
fi

sys_info=`/bin/sh $CONFIG_GUESS_PATH/config.guess`

echo "Building for $sys_info"

case "$sys_info" in
  *-ibm-aix4*     ) OS=AIX        ;;
  *-freebsd*      ) OS=FREEBSD    ;;
  hppa*-hp-hpux11*) OS=HPUX       ;;
  *-sgi-irix6*    ) OS=IRIX       ;;
  *-linux*        ) OS=LINUX      ;;
  *-netbsd*       ) OS=NETBSD     ;;
  *-openbsd*      ) OS=OPENBSD    ;;
  *-dec-osf*      ) OS=OSF1       ;;
  *-solaris2*     ) OS=SOLARIS    ;;
  *-darwin*       ) OS=DARWIN     ;;
  *               ) OS=
                    echo "Sorry, unsupported OS"
                    exit 1        ;;
esac

echo "Making with OS=$OS"

