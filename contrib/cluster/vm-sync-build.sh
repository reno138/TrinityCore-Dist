#!/usr/bin/env bash
# contrib/cluster/vm-sync-build.sh — rsync this tree to the build VM and build there.
# Usage: contrib/cluster/vm-sync-build.sh configure|build|install|all
set -euo pipefail
VM="${VM:-wow@192.0.2.10.20}"
REMOTE_SRC="${REMOTE_SRC:-/home/wow/source/c9core-tc}"
PREFIX="${PREFIX:-/home/wow/tc-335}"
JOBS="${JOBS:-8}"
LOCAL_SRC="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-all}"

sync() {
  rsync -az --delete \
    --exclude 'build/' --exclude '*.o' \
    "$LOCAL_SRC/" "$VM:$REMOTE_SRC/"
}
configure() {
  ssh "$VM" "mkdir -p $REMOTE_SRC/build && cd $REMOTE_SRC/build && \
    cmake .. -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTOOLS=1 -DSERVERS=1 -DSCRIPTS=static -DWITH_WARNINGS=0 -DBUILD_TESTING=1 \
      > configure.log 2>&1; tail -3 configure.log"
}
build() {
  ssh "$VM" "cd $REMOTE_SRC/build && (make -j$JOBS > build.log 2>&1; echo EXIT=\$? >> build.log); \
    grep -cE ' error:|Error [0-9]+' build.log | sed 's/^/errors: /'; tail -1 build.log"
}
install() {
  ssh "$VM" "cd $REMOTE_SRC/build && make install > install.log 2>&1; tail -1 install.log"
}
case "$MODE" in
  configure) sync; configure ;;
  build)     sync; build ;;
  install)   install ;;
  all)       sync; configure; build; install ;;
  *) echo "usage: $0 configure|build|install|all" >&2; exit 2 ;;
esac
