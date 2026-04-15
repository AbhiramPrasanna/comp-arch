#!/bin/bash

for file in ./output*
do
    if test -d $file
    then
        ./reg_output.py $file
    fi
done
