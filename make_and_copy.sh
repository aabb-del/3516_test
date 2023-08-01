#!/bin/bash
cd build
make

cd ..
./copy_to_ftp.sh
