#!/system/bin/sh

# Rather than have ldd and ldd64, this script does the right thing depending
# on the argument.

function error() {
  echo "$1"
  exit 1
}

[ $# -eq 1 ] || error "usage: ldd FILE"

case "$1" in
  /*)
    file="$1"
    ;;
  *)
    file="$(pwd)/$1"
    ;;
esac

what=$(LD_LIBRARY_PATH= file -L "$file")
case "$what" in
  *32-bit*)
    linker --list "$file"
    ;;
  *64-bit*)
    linker64 --list "$file"
    ;;
  *)
    error "$what"
    ;;
esac
