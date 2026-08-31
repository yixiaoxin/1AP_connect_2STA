#!/bin/sh -x

opt=$@

sh build_sntp_client.sh -libgen $opt
if [ $? -eq 0 ]; then
    echo '************************************succeed'
    mv  ../../../build/host-wifi-aic8800/armgcc_4_8/*.a         ./lib/armgcc_4_8
else
    echo '************************************failed'
    exit 1
fi

