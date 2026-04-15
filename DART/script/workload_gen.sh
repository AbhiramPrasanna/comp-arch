#!/usr/bin/env bash

Reset='\033[0m'
Bold='\033[1m'
Black='\033[90m'
Red='\033[91m'
Green='\033[92m'
Yellow='\033[93m'
Blue='\033[94m'
Purple='\033[95m'
Cyan='\033[96m'
White='\033[97m'

cd workload
mkdir data/
pattern="ycsb-*/"
files=( $pattern )
cd "${files[0]}"/bin
printf "${Blue}${Bold}"
pwd

# echo -e "${Red}${Bold}YCSB HELP${Reset}"
# ./ycsb

echo -e "${Blue}${Bold}YCSB RUN${Reset}"
for x in {a..g}
do
    echo -e "${Cyan}gen ${x}${Reset}"
    python2 ./ycsb load basic -P ../../../workload_spec/${x} -s > ../../../workload/data/${x}_load
    python2 ./ycsb run basic -P ../../../workload_spec/${x} -s > ../../../workload/data/${x}_run
done
