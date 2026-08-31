#!/bin/sh -x

opt=$@

sh build_bt_wifi_fhostif_case.sh SOFTAP=off -libgen $opt
if [ $? -eq 0 ]; then
    echo '************************************succeed'
    mv  ../../../build/host-wb-aic8800m40/armgcc_4_8/*.a         ./lib/armgcc_4_8
else
    echo '************************************failed'
    exit 1
fi

sh build_bt_wifi_fhostif_case.sh SOFTAP=on -libgen $opt
if [ $? -eq 0 ]; then
    echo '************************************succeed'
    mv  ../../../build/host-wb-aic8800m40/armgcc_4_8/*.a         ./lib/armgcc_4_8
else
    echo '************************************failed'
    exit 1
fi

