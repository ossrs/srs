#!/bin/sh
#
# format.sh
#
# run clang-format on each .c & .h file
#
# assumes git tree is clean when reporting status

if [ -z "${CLANG_FORMAT}" ]; then
    CLANG_FORMAT=clang-format
fi

a=`git ls-files '*.h' '*.c'`
for x in $a; do
    if [ $x != "config_in.h" ]; then
        $CLANG_FORMAT -i -style=file $x
    fi
done

m=`git ls-files -m`
if [ -n "$m" ]; then
    v=`$CLANG_FORMAT -version`
    echo "Fromatting required when checking with $v"
    echo
    echo "The following files required formatting:"
    for f in $m; do
        echo $f
    done
    if [ "$1" = "-d" ]; then
        echo
        git diff
    fi
    exit 1
fi
exit 0
