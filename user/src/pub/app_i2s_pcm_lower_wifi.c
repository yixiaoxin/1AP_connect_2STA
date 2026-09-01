#include <stdint.h>
#include "dbg.h"
#include "rtos.h"
#include "rtos_al.h"
#include "rtos_def.h"
#include "modules/app_audio_pcm.h"
#include "modules/app_audio_link.h"
#include "modules/app_i2s_ssd212_bridge.h"

#ifndef TRIANGLE_STA_ENTRY_DELAY_MS
#define TRIANGLE_STA_ENTRY_DELAY_MS 100U
#endif

static void triangle_sta_user_task(void *param)
{
    int ret;
    (void)param;
    rtos_task_suspend(TRIANGLE_STA_ENTRY_DELAY_MS);

    dbg("TRI%u boot v7.0.12R18: capture/playback 48k stereo, record wire 16k stereo 10ms\r\n",
        (unsigned)TRIANGLE_DEVICE_ID);

    app_audio_pcm_init();
    ret = app_audio_link_init();
    if (ret != 0) {
        dbg("SYSTEM: TRI%u audio link init failed ret=%d\r\n",
            (unsigned)TRIANGLE_DEVICE_ID, ret);
        rtos_task_delete(NULL);
        return;
    }
    ret = app_audio_link_start();
    if (ret != 0) {
        dbg("SYSTEM: TRI%u audio link start failed ret=%d\r\n",
            (unsigned)TRIANGLE_DEVICE_ID, ret);
        rtos_task_delete(NULL);
        return;
    }
    ret = app_i2s_ssd212_bridge_start();
    if (ret != 0) {
        dbg("SYSTEM: TRI%u I2S start failed ret=%d\r\n",
            (unsigned)TRIANGLE_DEVICE_ID, ret);
        app_audio_link_stop();
        rtos_task_delete(NULL);
        return;
    }
    dbg("TRI%u READY audio bridge started\r\n",
        (unsigned)TRIANGLE_DEVICE_ID);
    rtos_task_delete(NULL);
}

void user_code_entry(void)
{
    if (rtos_task_create(triangle_sta_user_task,
                         "TRI_ENTRY",
                         USER_CODE_TASK,
                         2048U,
                         NULL,
                         TASK_PRIORITY_USER_CODE,
                         NULL)) {
        dbg("SYSTEM: Triangle STA%u entry task create failed\r\n",
            (unsigned)TRIANGLE_DEVICE_ID);
    }
}
