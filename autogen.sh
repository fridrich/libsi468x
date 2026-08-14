#!/bin/sh
# Bootstrap Autotools configurations
set -e

mkdir -p m4
echo "Running autoreconf..."
autoreconf -fi
echo "Autotools bootstrap completed successfully. Now run ./configure and make."
