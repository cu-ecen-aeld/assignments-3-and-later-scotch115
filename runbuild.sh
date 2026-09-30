#!/bin/sh
clear
echo "Starting automated testing"

./cleanbuild.sh

# clear

echo "Unloading & Reloading aesdchar driver"
./aesd-char-driver/aesdchar_unload
./aesd-char-driver/aesdchar_load

# clear

echo "Running drivertest.sh"
./assignment-autotest/test/assignment9/drivertest.sh

echo "Unloading & Reloading aesdchar driver"
./aesd-char-driver/aesdchar_unload
./aesd-char-driver/aesdchar_load

echo "== Starting aesdsocket server =="
./server/aesdsocket &
AESDSOCKET=$!

echo "Running sockettest.sh"
./assignment-autotest/test/assignment9/sockettest.sh

echo "Complete! Killing aesdsocket server background task"
kill $AESDSOCKET

echo "Unloading aesdchar driver"
./aesd-char-driver/aesdchar_unload
