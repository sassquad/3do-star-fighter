#!/bin/sh
# Stand-in for cmd.exe, used as SHELL= by the MinGW build check in tests/run.py.
# mingw32-make run from a Windows command prompt executes recipes with cmd.exe,
# whose mkdir has no -p option ('-p' becomes a directory), rejects '/' in paths
# and fails if the directory exists.  This script gives "mkdir" those rules, so
# a recipe that would break the Windows build fails here too.  Everything else
# runs in sh.
[ "$1" = "-c" ] && shift
cmdline="$1"
case "$cmdline" in
  mkdir\ *|@mkdir\ *)
    set -- $cmdline; shift
    for a in "$@"; do
      case "$a" in */*) echo "The syntax of the command is incorrect." >&2; exit 1;; esac
    done
    for a in "$@"; do
      if [ -e "$a" ]; then echo "A subdirectory or file $a already exists." >&2; exit 1; fi
      /bin/mkdir -- "$a" || exit 1
    done
    exit 0;;
esac
exec /bin/sh -c "$cmdline"
