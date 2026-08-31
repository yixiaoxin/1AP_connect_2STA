clear

if [ $1 = "all" ]
then
	chmod +x build_btdm_wifi_8800m40.sh
	./build_btdm_wifi_8800m40.sh ASIO=on HCLK_MCLK=on USER_CODE=src TEST=tinyusb TUSB_DEMO=audio_uac -j2
	cp -v ../../../build/host-wb-aic8800m40/btdm_wifi_8800m40.bin /mnt/hgfs/share/
else
	rm -rf ../../../build/host-wb-aic8800m40/btdm_wifi_8800m40.bin
	rm -rf /mnt/hgfs/share/btdm_wifi_8800m40.bin
fi
