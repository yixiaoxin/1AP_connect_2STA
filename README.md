这是STA1侧的工程文件，主要做了一下修改：

1，把MAC地址固定成了88:00:33:AA:BB:C1

/* Fixed STA MAC address, persisted to flash.  Tail byte is derived from
     * TRIANGLE_DEVICE_ID (1 -> 88:00:33:AA:BB:C1, 2 -> ...:C2) so the two
     * STAs built from this source do not collide on the same network. */
     
    {
        uint8_t fixed_mac[6] = {0x88, 0x00, 0x33, 0xAA, 0xBB,
                                0xC0U | (uint8_t)TRIANGLE_DEVICE_ID};
        set_mac_address(fixed_mac);
        flash_wifi_sta_macaddr_write(fixed_mac);
    }
