clear

if [ $1 = "all" ]
then
	chmod +x build_wifi_case_8800m40.sh
	./build_wifi_case_8800m40.sh HCLK_MCLK=on USER_CODE=src -j2
	cp -v ../../../build/host-wifi-aic8800m40/host_wb_aic8800m40.bin /mnt/hgfs/share/
else
	rm -rf ../../../build/host-wifi-aic8800m40/host_wb_aic8800m40.bin
	rm -rf /mnt/hgfs/share/host_wifi_aic8800m40.bin
fi
