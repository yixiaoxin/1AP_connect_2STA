#!/bin/sh -x

opt=$@

sh build_bt_source_and_ag.sh -libgen $opt
if [ $? -eq 0 ]; then
    echo '************************************succeed'
    mv  ../../../build/host-bt-aic8800m40/armgcc_4_8/*.a         ./lib/armgcc_4_8
else
    echo '************************************failed'
    exit 1
fi


