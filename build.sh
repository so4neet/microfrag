#! /bin/bash
. /opt/toolchains/dc/kos/environ.sh

BUILDARCH=$(uname -m)
ARCH=0
BLDNUM=$(git rev-list --full-history --all --abbrev-commit | wc -l)
GITHSH=$(git rev-list --full-history --all --abbrev-commit | head -1)
echo '#define VERSION "'$BLDNUM::$GITHSH.$BUILDARCH'"'> src/version.h
make
