#!/bin/sh
clear

echo "Cleaning and building aesd-char-driver"
cd ./aesd-char-driver
make clean && make

echo "Cleaning and building aesdsocket server"
cd ../server
make clean && make

echo "Returning to root directory"
cd ..
