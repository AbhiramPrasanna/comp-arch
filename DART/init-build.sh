#!/usr/bin/env bash

git submodule update --init --recursive

rm -rf build
cmake -B build
# cmake -B build -DCMAKE_CXX_COMPILER=g++-11
cmake --build build

ulimit -a
# ulimit -l unlimited

grep -i hugepages /proc/meminfo
# sudo sysctl -w vm.nr_hugepages=16384  # 16384 * Hugepagesize (2MiB) == 32GiB
