#!/bin/bash
#
# Compatibility wrapper around bang.py, which does the real work: it finds
# each board's serial port by its MAC (boards.toml), so swapped /dev/ttyACM*
# ports no longer matter.
#
#   bash ./build.sh deploy banger button        same as: python3 bang.py deploy banger button
#   EXTRA_FLAGS='-ULOG_VERBOSE -DBANGER' bash ./build.sh deploy   (old style: role from the flags)
#
# Old variables still work: EXTRA_FLAGS, DEBUG=1, DEV_PORT (skips the MAC lookup),
# ARDUINO_DIRECTORIES_DATA.

HERE=$(dirname $(readlink -f $BASH_SOURCE))
BANG="python3 $HERE/bang.py"

cmd=$1
shift
targets=("$@")
if [ ${#targets[@]} = 0 ]; then
    # old style: the role comes from -DBANGER / -DBUTTON / -DKILLSWITCH in EXTRA_FLAGS
    for r in BANGER BUTTON KILLSWITCH; do
        [[ " $EXTRA_FLAGS " == *" -D$r "* ]] && targets+=($r)
    done
fi
port=()
[ -n "$DEV_PORT" ] && port=(--port "$DEV_PORT")

case $cmd in
    build|compile)  exec $BANG build "${targets[@]}" ;;
    upload|flash)   exec $BANG flash "${targets[@]}" "${port[@]}" ;;
    deploy)         exec $BANG deploy "${targets[@]}" "${port[@]}" ;;
    all)            exec $BANG deploy "${targets[@]}" "${port[@]}" --monitor ;;
    monitor)        exec $BANG monitor "${targets[@]}" "${port[@]}" ;;
    list|identify|update)
                    exec $BANG $cmd "${targets[@]}" ;;
    help|--help|-h|"")
        echo "Usage: $0 {list|identify PORT|update|build|upload|deploy|all|monitor} [board|role|all ...]"
        echo
        exec $BANG --help ;;
    *)
        echo "Unknown command: $cmd (try: $0 help)"
        exit 1 ;;
esac
