#!/bin/sh
# Run the supplied test binary on a private bus so it cannot observe or disturb
# a live daemon.  Example: ./run-pebbles-async-test.sh /tmp/build/pebbles_async_test
if [ "$#" -eq 0 ]; then
    echo "usage: $0 /path/to/pebbles_async_test [QtTest arguments...]" >&2
    exit 2
fi

exec dbus-run-session -- "$@"
