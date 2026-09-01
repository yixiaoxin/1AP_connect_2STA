/*
 * Copyright (C) 2018-2023 AICSemi Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Includes
 */
#include "dbg.h"
#include "demo_lib.h"
#include "demo_src.h"
#include "rtos.h"
#include "rtos_al.h"
/*2025.7.18 first test */
#include "gpio_api.h"    /*添加gpio驱动头文件*/
#include "lp_ticker_api.h"  /*添加定时器驱动头文件 */
#include "i2cm_api.h"
#include "sysctrl_api.h"
#include "fhost_wpa.h"
#include "fhost.h"
#include "wlan_user.h"
#include "wlan_if.h"
#include <limits.h> 

#include <stdio.h>
#include <errno.h>
#include "netdb.h"
#include "fhost_config.h"
#include "sleep_api.h"
#include "sysctrl_api.h"
#include "system.h"
#include "console.h"

#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"

#include "lwip/tcp.h"
#include "lwip/tcpip.h"

#include "lwip/sockets.h"
#include "fhost.h"
#include "rtos_def.h"
#include "rtos_al.h"
#include "rtos.h"

#include "rtos_def.h"
#include "rtos_al.h"
#include "rtos.h"
#include "plf.h"
#include <string.h>
#include "boot.h"
#include "sleep_api.h"
#include "lp_ticker_api.h"

#include "portable.h"

#include <malloc.h>

// 使用us_ticker高精度计时
#include "us_ticker_api.h"

//音频需要
#include "asio.h"
#include "app_media_mgr.h"
#include "gpio_api.h"
#include "audio_proc_api.h"

//-----------------------2026-4-23------------------------------------------------------------------------
//#include "modules/app_aic_power_adc.h"




// GPIO配置（根据实际连接调整）
// #define CODEC_RESET_PIN    GPIO_PIN_12        // 编解码器复位引脚
#define I2S0_BCK_PIN      1         // I2S0位时钟
#define I2S0_WS_PIN       0         // I2S0帧时钟
#define I2S0_TX_PIN       3         // I2S0发送数据
#define I2S0_RX_PIN       2         // I2S0接收数据

#define UDPSelect    0
#define TCPSelect    1

#if UDPSelect
static uint16_t remote_port = 8888;  //通信端口
static uint16_t local_port = 8888;  //本地监听端口
static int udp_socket = -1;
static rtos_task_handle udp_task_handle = NULL;
#endif

// gpio测试配置
#define GPIO_TEST_PIN           12
#define GPIO_TEST_DELAY_MS      500
#define GPIO_TEST_MAX_LOOPS     100

// i2c测试配置
#define I2C_DEV_MPU6050         0
#define I2C_DEV_ADXL345         1       // Support Multi byte read test

#define I2CM_DMA_TEST           1       // Multi byte write and read
#define I2CM_DMA_TX_LEN         128     // Can`t longer than 128
#define I2CM_DMA_RX_LEN         128     // Can`t longer than 128
#define USER_PRINTF             dbg

void gpio_test_task(void);

#ifdef CFG_SOFTAP
extern int ps_sta_connected;
extern uint8_t is_ap;
#endif

#define TEST_SOFTAP     1
#define TEST_CONNECT    0
#define TEST_SCAN       0
#define TEST_MONITOR   (0 && NX_FHOST_MONITOR)
#define TRACE_APP(...)  do {} while (0)
#define CONFIG_AUTO_PING    0

#if (TEST_SCAN || TEST_MONITOR)
/// Link parameters
static struct fhost_cntrl_link *cntrl_link;

#endif /* TEST_SCAN */


#if PLF_WIFI_STACK
#include "ipc_host.h"
#endif

rtos_task_handle gpioflash;


static volatile uint32_t count = 0;

// 典型定义
#define US_TICKER_FREQ     1000000  // 1MHz时钟 = 1us分辨率

// 根据网络状况调整
#define TEST_PACKET_SIZE     1460    // 以太网MTU
#define TEST_PACKET_COUNT    10000   // 初始值

// UDP测试配置
#define UDP_PORT             8888
#define PACKET_SIZE          1400     // 每个UDP包的大小
#define TEST_DURATION_MS     10000    // 测试持续时间10秒
#define AP_IP           "192.168.88.1" // AP固定IP



#define PACKETS_PER_SECOND 5000 // 每秒发送的包数

// 调试信息打印间隔(ms)
#define STATS_INTERVAL 1000

// UDP包结构(包含序列号和时间戳)
typedef struct {
    uint32_t seq_num;      // 序列号
    uint64_t timestamp_us; // 发送时间戳(微秒)
    uint8_t data[PACKET_SIZE - sizeof(uint32_t) - sizeof(uint64_t)]; // 数据
} udp_packet_t;

// 全局统计结构
struct {
    uint32_t total_packets;
    uint32_t total_bytes;
    uint64_t start_time;
    uint64_t last_report_time;
} stats;




// 实现时间获取函数
uint64_t get_us_time(void) {
    static uint32_t last = 0;
    static uint64_t total = 0;
    uint32_t now = us_ticker_read();
    
    // 处理计数器溢出（每4295秒发生一次）
    if (now < last) {
        
        total += 0x100000000; // 增加一个32位溢出周期
    }
    last = now;
    return total + now; // 返回64位累计时间
}


void test_time_func() {
    uint64_t t1 = get_us_time();
    rtos_task_suspend(1000); // 等待1秒
    uint64_t t2 = get_us_time();
    dbg("Delta time: %llu us (expected 1s)", t2 - t1);
}


// 基于现有 rtos_now() 封装
static inline uint64_t rtos_get_time(void) {
    return (uint64_t)rtos_now(false) * 1000; // 转换为微秒
}
#if 0
// 全局配置
#define AUDIO_BUF_SIZE 2920 
#define HALF_BUF_SIZE (AUDIO_BUF_SIZE / 2)

// 独立音频缓冲区 (解决共享冲突)
static uint8_t tx_audio_buf[AUDIO_BUF_SIZE];
static uint8_t rx_audio_buf[AUDIO_BUF_SIZE];
#endif

#define TCP_PORT 8888
// #define AUDIO_BUF_SIZE 2888
// static uint8_t audio_buf[AUDIO_BUF_SIZE];

// AP端全局变量定义
static uint8_t target_sta_id = 1;  // 默认与ID为1的STA进行双向传输
static rtos_mutex target_id_mutex = NULL;


// 音频流方向定义
// ====================== 音频方向 & 包头定义（AP / STA 公用） ======================

// 音频流方向定义
#define AUDIO_DIRECTION_AP_TO_STA 0x01  // AP采集 → STA播放
#define AUDIO_DIRECTION_STA_TO_AP 0x02  // STA采集 → AP播放

// 增强版音频包头
#pragma pack(push, 1)
typedef struct {
    uint32_t seq_num;           // 序列号
    uint64_t timestamp;         // 时间戳(微秒)
    uint8_t  direction;         // 数据流向
    uint8_t  client_id;         // 客户端标识
    uint16_t data_len;          // 数据长度
} audio_header_t;
#pragma pack(pop)

// 统一音频帧大小
#define AUDIO_BUF_SIZE 2888

// AP / STA 共用的时间函数
uint64_t get_us_time(void);     


// ==========================================================================
//                              AP 端（SoftAP 固件使用）
// ==========================================================================

#if TEST_SOFTAP

// ---- AP 端音频缓冲区 ----
static uint8_t ap_capture_buf[AUDIO_BUF_SIZE];      // AP 采集用 buffer（发给各个 STA）
static uint8_t ap_playback_buf[AUDIO_BUF_SIZE];     // AP 播放用 buffer（给 ASIO 用）

// ---- 多 STA 支持 ----
#define MAX_STA_COUNT 8


typedef struct {
    int      fd;                // TCP 连接 fd
    uint8_t  id;                // STA ID（1~MAX_STA_COUNT）
    bool     connected;         // 是否连接
    bool     has_recent_audio;  // 最近是否有收到音频
    uint32_t packet_counter;    // 给这个 STA 发包用的序号
    uint32_t last_data_len;     // 最近一帧音频长度（字节）
} sta_info_t;

// 最多同时参与混音的 STA 数
#define MAX_MIX_STA              8     

// 静音多久认为“说完话了”，把名额让出来（单位: 微秒）
#define MIX_SILENCE_TIMEOUT_US   200000  


typedef struct {
    bool     used;              // 这个混音槽是否被占用
    int      sta_index;         // 对应的 sta_list[] 下标
    uint8_t  buf[AUDIO_BUF_SIZE]; // 保存该 STA 最近一帧音频
    uint32_t len;               // 最近一次写入的有效长度
    uint64_t last_update_us;    // 最近一次收到该 STA 数据的时间戳
} mix_slot_t;


// 全局混音槽数组
static mix_slot_t mix_slots[MAX_MIX_STA];

static sta_info_t sta_list[MAX_STA_COUNT];


// 互斥锁
static rtos_mutex  ap_buffer_mutex      = NULL;     // 保护 ap_mix_buf / current_talker
static rtos_mutex  sta_list_mutex       = NULL;     // 保护 sta_list

static int         ap_server_fd         = -1;       // AP TCP 服务器 fd
static bool        ap_asio_initialized  = false;
static uint32_t    short_dbg_cnt        = 0;        // 控制打印频率


// 找到某个 STA 已经占用的混音槽，没有则返回 -1
static int find_mix_slot_for_sta(int sta_idx)
{
    for (int i = 0; i < MAX_MIX_STA; i++) {
        if (mix_slots[i].used && mix_slots[i].sta_index == sta_idx) {
            return i;
        }
    }
    return -1;
}

// 为某个 STA 分配一个混音槽（如果已经有就直接返回那个）
// 若混音槽已满则返回 -1
static int alloc_mix_slot_for_sta(int sta_idx)
{
    int slot = find_mix_slot_for_sta(sta_idx);
    if (slot >= 0) {
        // dbg("AP: STA(idx=%d, id=%u) already using mix slot %d, buffer address: 0x%p,mix slot address: 0x%p\n",
        //     sta_idx, sta_list[sta_idx].id, slot, mix_slots[slot].buf,&mix_slots[slot]);
        return slot;
    }

    // 找一个空槽
    for (int i = 0; i < MAX_MIX_STA; i++) {
        if (!mix_slots[i].used) {
            mix_slots[i].used          = true;
            mix_slots[i].sta_index     = sta_idx;
            mix_slots[i].len           = 0;
            mix_slots[i].last_update_us= 0;

            dbg("AP: STA(idx=%d, id=%u) allocated mix slot %d, buffer address: 0x%p, mix slot address: 0x%p,size: %d bytes\n",
                sta_idx, sta_list[sta_idx].id, i, mix_slots[i].buf, &mix_slots[i],AUDIO_BUF_SIZE);
            // dbg("AP: STA(idx=%d, id=%u) take mix slot %d\n",
            //     sta_idx, sta_list[sta_idx].id, i);
            return i;
        }
    }

    // 没空槽了
    return -1;
}

// 释放某个 STA 占的所有混音槽（一般只有一个，为安全写成循环）
static void release_mix_slots_for_sta(int sta_idx)
{
    for (int i = 0; i < MAX_MIX_STA; i++) {
        if (mix_slots[i].used && mix_slots[i].sta_index == sta_idx) {
            dbg("AP: release mix slot %d for STA idx=%d (id=%u)\n",
                i, sta_idx, sta_list[sta_idx].id);
            mix_slots[i].used          = false;
            mix_slots[i].sta_index     = -1;
            mix_slots[i].len           = 0;
            mix_slots[i].last_update_us= 0;
        }
    }
}

// ------------------------- 从 STA 接收音频数据 -------------------------
static uint32_t ap_recv_from_sta(int idx)
{
    sta_info_t *sta = &sta_list[idx];

    if (!sta->connected || sta->fd < 0) {
        return 0;
    }

    audio_header_t header;
    uint8_t        recv_buf[sizeof(header) + AUDIO_BUF_SIZE];

    int len = recv(sta->fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);

    if (len == 0) {
        // 对端正常关闭
        dbg("AP: STA%d closed (fd=%d)\n", sta->id, sta->fd);

        rtos_mutex_lock(ap_buffer_mutex, -1);
        release_mix_slots_for_sta(idx);
        rtos_mutex_unlock(ap_buffer_mutex);

        close(sta->fd);
        sta->fd           = -1;
        sta->connected    = false;
        sta->has_recent_audio = false;
        return 0;
    }

    if (len < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            // 当前没有数据
            return 0;
        }

        dbg("AP: recv error from STA%d, errno=%d\n", sta->id, err);

        rtos_mutex_lock(ap_buffer_mutex, -1);
        release_mix_slots_for_sta(idx);
        rtos_mutex_unlock(ap_buffer_mutex);

        close(sta->fd);
        sta->fd           = -1;
        sta->connected    = false;
        sta->has_recent_audio = false;
        return 0;
    }

    if (len < (int)sizeof(header)) {
        if ((short_dbg_cnt++ % 50) == 0) {
            dbg("AP: short packet from STA%d, len=%d\n", sta->id, len);
        }

        return 0;
    }

    memcpy(&header, recv_buf, sizeof(header));

    //第一次收到这个 STA 的包时，用它上报的 client_id 绑定 ID
    if (sta->id == 0) {
        sta->id = header.client_id;
        dbg("AP: bind slot %d to STA id=%u\n", idx, sta->id);
    } else if (sta->id != header.client_id) {
        // 如果后面 client_id 变了，打印一下告警（一般不应该发生）
        dbg("AP: WARN slot %d expect id=%u but got client_id=%u\n",
            idx, sta->id, header.client_id);
    }

    // 为了日志友好，如果还没绑定 ID，就用槽位号 +1 打印
    uint8_t log_id = (sta->id != 0) ? sta->id : (idx + 1);

    // if ((short_dbg_cnt++ % 1000) == 0) {
    //     dbg("AP: RX from STA%d seq=%u len=%u dir=%u cid=%u\n",
    //         log_id, header.seq_num, header.data_len,
    //         header.direction, header.client_id);
    // }
    //测延时
    dbg("AP: RX from STA%d seq=%u len=%u dir=%u cid=%u time=%u\n",
            log_id, header.seq_num, header.data_len,
            header.direction, header.client_id,header.timestamp);

   if (header.direction == AUDIO_DIRECTION_STA_TO_AP) {
        uint8_t *audio = recv_buf + sizeof(header);

        rtos_mutex_lock(ap_buffer_mutex, -1);

        // 为这个 STA 找/分配一个混音槽
        int slot = alloc_mix_slot_for_sta(idx);
        if (slot < 0) {
            // 没有空的混音槽了，这个 STA 不参与混音，直接丢弃音频
            if ((short_dbg_cnt++ % 200) == 0) {
                dbg("AP: drop audio from STA idx=%d (id=%u), no free mix slot\n",
                    idx, sta->id);
            }
        } else {
            uint32_t copy_len = header.data_len;
            if (copy_len > AUDIO_BUF_SIZE) {
                copy_len = AUDIO_BUF_SIZE;
            }
            memcpy(mix_slots[slot].buf, audio, copy_len);
            mix_slots[slot].len           = copy_len;
            mix_slots[slot].last_update_us= get_us_time();
            sta->has_recent_audio         = true;
        }

        rtos_mutex_unlock(ap_buffer_mutex);
    } else {
        dbg("AP: unexpected direction %d from STA%d\n",
            header.direction, sta->id);
    }

    return (uint32_t)len;
}


// ------------------------- AP 采集回调：发给所有 STA -------------------------
static uint32_t ap_capture_callback(uint8_t *buf, uint32_t len)
{
    rtos_mutex_lock(sta_list_mutex, -1);

    for (int i = 0; i < MAX_STA_COUNT; i++) {
        if (!sta_list[i].connected || sta_list[i].fd <= 0 || sta_list[i].id == 0) {
            continue;
        }

        audio_header_t header = {
            .seq_num   = sta_list[i].packet_counter++,
            .timestamp = get_us_time(),
            .direction = AUDIO_DIRECTION_AP_TO_STA,
            .client_id = sta_list[i].id,
            .data_len  = len
        };

        uint8_t send_buf[sizeof(header) + AUDIO_BUF_SIZE];
        memcpy(send_buf, &header, sizeof(header));
        memcpy(send_buf + sizeof(header), buf, len);

        int ret = send(sta_list[i].fd, send_buf, sizeof(header) + len, MSG_DONTWAIT);
        if (ret < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                dbg("AP: TX fail, close STA%d errno=%d\n", sta_list[i].id, err);
                close(sta_list[i].fd);
                sta_list[i].fd            = -1;
                sta_list[i].connected     = false;
                sta_list[i].has_recent_audio = false;
            }
        } else {
            if ((short_dbg_cnt++ % 1000) == 0) {
                dbg("AP: TX to STA%d ret=%d seq=%u\n",
                    sta_list[i].id, ret, header.seq_num);
            }
        }
    }

    rtos_mutex_unlock(sta_list_mutex);

    return len;
}

// ------------------------- AP 播放回调：播放当前说话的 STA -------------------------
// 16bit 饱和函数
static inline int16_t clamp_16(int32_t v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static uint32_t ap_playback_callback(uint8_t *buf, uint32_t len)
{
    // 先全静音，避免残留
    memset(buf, 0, len);

    uint64_t now_us = get_us_time();

    rtos_mutex_lock(ap_buffer_mutex, -1);

    int active_slots[MAX_MIX_STA];
    int active_cnt = 0;

    // 先挑出“还在说”的混音槽，并顺便做超时释放
    for (int i = 0; i < MAX_MIX_STA; i++) {
        if (!mix_slots[i].used) {
            continue;
        }

        // 超时没数据，释放坑
        if (now_us - mix_slots[i].last_update_us > MIX_SILENCE_TIMEOUT_US) {
            dbg("AP: mix slot %d timeout, release STA idx=%d id=%u\n",
                i, mix_slots[i].sta_index,
                (mix_slots[i].sta_index >= 0) ? sta_list[mix_slots[i].sta_index].id : 0);
            mix_slots[i].used          = false;
            mix_slots[i].sta_index     = -1;
            mix_slots[i].len           = 0;
            mix_slots[i].last_update_us= 0;
            continue;
        }

        if (mix_slots[i].len == 0) {
            continue;
        }

        active_slots[active_cnt++] = i;
    }

    if (active_cnt == 0) {
        // 没人说话，保持静音
        rtos_mutex_unlock(ap_buffer_mutex);
        return len;
    }

    // === 16bit 混音 ===
    uint32_t sample_count = len / 2;
    int16_t *out = (int16_t *)buf;

    for (uint32_t s = 0; s < sample_count; s++) {
        int32_t acc = 0;
        int      valid_src = 0;

        for (int k = 0; k < active_cnt; k++) {
            mix_slot_t *slot = &mix_slots[active_slots[k]];

            // 不够这一采样位置的数据就当 0
            if (slot->len < (s + 1) * 2) {
                continue;
            }

            int16_t *src = (int16_t *)slot->buf;
            acc += src[s];
            valid_src++;
        }

        if (valid_src == 0) {
            out[s] = 0;
        } else {
            // 简单平均，避免爆音
            acc /= valid_src;
            out[s] = clamp_16(acc);
        }
    }

    rtos_mutex_unlock(ap_buffer_mutex);

    return len;
}


// ----------------------------- AP 端 TCP 音频服务器任务 -----------------------------
void ap_tcp_server_task(void *arg)
{
    struct sockaddr_in server_addr = {0};
    int res;

    dbg("=========================\n");
    dbg(" AP Multi-STA Audio Server Start\n");
    dbg("=========================\n");

    // 初始化互斥锁
    if (ap_buffer_mutex == NULL)
        rtos_mutex_create(&ap_buffer_mutex);

    if (sta_list_mutex == NULL)
        rtos_mutex_create(&sta_list_mutex);

    // 初始化 STA 列表
    for (int i = 0; i < MAX_STA_COUNT; i++) {
        sta_list[i].fd              = -1;
        sta_list[i].id              = 0;
        sta_list[i].connected       = false;
        sta_list[i].has_recent_audio= false;
        sta_list[i].packet_counter  = 0;
    }

    // 初始化混音槽
    for (int i = 0; i < MAX_MIX_STA; i++) {
        mix_slots[i].used          = false;
        mix_slots[i].sta_index     = -1;
        mix_slots[i].len           = 0;
        mix_slots[i].last_update_us= 0;

    // 直接打印每个混音槽缓冲区的起始地址
    dbg("AP: Mix slot %d - Mix slot address:0x%p - Buffer start address: 0x%p, Size: %d bytes\n", 
        i, &mix_slots[i],mix_slots[i].buf, AUDIO_BUF_SIZE);
    }

    dbg("AP: STA list initialized, total slots=%d, mix slots=%d\n",
    MAX_STA_COUNT, MAX_MIX_STA);

    // 初始化 ASIO 音频系统
    asio_init();
    res = asio_open();
    if (res != ASIO_ERR_NONE) {
        dbg("AP: asio_open failed: %d\n", res);
        goto cleanup;
    }
    ap_asio_initialized = true;
    dbg("AP: ASIO opened successfully\n");

    // 配置采集
    ASIO_STREAM_CFG_T ap_capture_cfg = {
        .path          = AUD_PATH_RX01,
        .device        = AUD_DEVICE_EXT_CODEC_I2S0,
        .device_role   = AUD_DEVICE_ROLE_MASTER,
        .bits          = AUD_BITS_24,
        .ch_num        = AUD_CH_NUM_2,
        .samp_rate     = AUD_SAMPRATE_16000,
        .src_samp_rate = AUD_SAMPRATE_48000,
        .buf_ptr       = ap_capture_buf,
        .buf_size      = AUDIO_BUF_SIZE,
        .handler       = ap_capture_callback,
        .src_en        = true
    };

    // 配置播放
    ASIO_STREAM_CFG_T ap_playback_cfg = {
        .path          = AUD_PATH_TX01,
        .device        = AUD_DEVICE_EXT_CODEC_I2S0,
        .device_role   = AUD_DEVICE_ROLE_MASTER,
        .bits          = AUD_BITS_24,
        .ch_num        = AUD_CH_NUM_2,
        .samp_rate     = AUD_SAMPRATE_16000,
        .src_samp_rate = AUD_SAMPRATE_48000,
        .buf_ptr       = ap_playback_buf,
        .buf_size      = AUDIO_BUF_SIZE,
        .handler       = ap_playback_callback,
        .src_en        = true
    };

    // 打开流
    if ((res = asio_stream_open(AUD_STREAM_GROUP_0, AUD_STREAM_CAPTURE, &ap_capture_cfg)) != ASIO_ERR_NONE) {
        dbg("AP: capture stream open failed: %d\n", res);
        goto cleanup;
    }

    if ((res = asio_stream_open(AUD_STREAM_GROUP_0, AUD_STREAM_PLAYBACK, &ap_playback_cfg)) != ASIO_ERR_NONE) {
        dbg("AP: playback stream open failed: %d\n", res);
        goto close_capture;
    }

    // 启动流
    if ((res = asio_stream_start(AUD_STREAM_GROUP_0, AUD_STREAM_CAPTURE)) != ASIO_ERR_NONE) {
        dbg("AP: capture start failed: %d\n", res);
        goto close_playback;
    }

    if ((res = asio_stream_start(AUD_STREAM_GROUP_0, AUD_STREAM_PLAYBACK)) != ASIO_ERR_NONE) {
        dbg("AP: playback start failed: %d\n", res);
        goto stop_capture;
    }

    // 创建 TCP 服务器
    ap_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ap_server_fd < 0) {
        dbg("AP: socket creation failed: %d\n", errno);
        goto stop_playback;
    }

    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ap_server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        dbg("AP: bind failed: %d\n", errno);
        goto close_server;
    }

    listen(ap_server_fd, 10);

    dbg("AP: Audio server listening on TCP port %d\n", TCP_PORT);

    // 主循环：接受 STA + 轮询
    while (1) {
        // 1. 非阻塞检查是否有新连接
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(ap_server_fd, &rfds);
        tv.tv_sec  = 0;
        tv.tv_usec = 0;

        int sel = select(ap_server_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(ap_server_fd, &rfds)) {
            int newfd = accept(ap_server_fd, NULL, NULL);
            if (newfd >= 0) {
                dbg("AP: New connection fd=%d\n", newfd);

                rtos_mutex_lock(sta_list_mutex, -1);

                bool assigned = false;
                for (int i = 0; i < MAX_STA_COUNT; i++) {
                    if (!sta_list[i].connected) {
                        sta_list[i].fd              = newfd;
                        sta_list[i].id              = 0; 
                        sta_list[i].connected       = true;
                        sta_list[i].has_recent_audio= false;
                        sta_list[i].packet_counter  = 0;

                        dbg("AP: slot %d connected (fd=%d, waiting for client_id)\n",
                             i, newfd);

                        assigned = true;
                        break;
                    }
                }

                if (!assigned) {
                    dbg("AP: All STA slots full, reject fd=%d\n", newfd);
                    close(newfd);
                }

                rtos_mutex_unlock(sta_list_mutex);
            } else {
                dbg("AP: accept error %d\n", errno);
            }
        }

        // 2. 轮询所有已连接 STA（收音频）
        rtos_mutex_lock(sta_list_mutex, -1);

        for (int idx = 0; idx < MAX_STA_COUNT; idx++) {
            if (sta_list[idx].connected && sta_list[idx].fd > 0) {
                if ((short_dbg_cnt++ % 5000) == 0) {
                    dbg("AP: Polling STA%d\n", sta_list[idx].id);
                }
                ap_recv_from_sta(idx);
            }
        }

        rtos_mutex_unlock(sta_list_mutex);

        // 3. 让出 CPU，控制轮询频率
        rtos_task_suspend(1);   // 1 ms
    }

    // ====== 错误退出处理 ======
close_server:
    close(ap_server_fd);

stop_playback:
    asio_stream_stop(AUD_STREAM_GROUP_0, AUD_STREAM_PLAYBACK);

stop_capture:
    asio_stream_stop(AUD_STREAM_GROUP_0, AUD_STREAM_CAPTURE);

close_playback:
    asio_stream_close(AUD_STREAM_GROUP_0, AUD_STREAM_PLAYBACK);

close_capture:
    asio_stream_close(AUD_STREAM_GROUP_0, AUD_STREAM_CAPTURE);

cleanup:
    asio_close();
    ap_asio_initialized = false;

    dbg("AP: Audio server stopped\n");

    rtos_task_delete(NULL);
}

#endif // TEST_SOFTAP



// ==========================================================================
//                              STA 端（Station 固件使用）
// ==========================================================================

#if TEST_CONNECT

// STA 端全局变量
static int      sta_sockfd          = -1;
static bool     sta_asio_initialized= false;
static volatile bool sta_connected  = false;
static uint8_t  sta_client_id       = 1;         // 每个 STA 配不同 ID（1、2、3...）
static uint32_t sta_packet_counter  = 0;

// ASIO 用 buffer（DMA）
static uint8_t  sta_capture_buf[AUDIO_BUF_SIZE];
static uint8_t  sta_playback_buf[AUDIO_BUF_SIZE];

// 网络接收用双缓冲
static uint8_t  sta_net_buf[2][AUDIO_BUF_SIZE];
static volatile int sta_net_active_buf = 0;

// STA 静音控制
static volatile bool   sta_selected       = false; // 是否被 AP 选中播放
static rtos_mutex      sta_buffer_mutex   = NULL;  // 保护 sta_net_buf
static rtos_mutex      sta_selected_mutex = NULL;  // 保护 sta_selected


// ------------------------- STA 采集回调：发给 AP -------------------------
static uint32_t sta_capture_callback(uint8_t *buf, uint32_t len)
{
    if (sta_connected && sta_sockfd != -1) {
        audio_header_t header = {
            .seq_num   = sta_packet_counter++,
            .timestamp = get_us_time(),
            .direction = AUDIO_DIRECTION_STA_TO_AP,
            .client_id = sta_client_id,
            .data_len  = len
        };

        uint8_t send_buf[sizeof(header) + AUDIO_BUF_SIZE];
        memcpy(send_buf, &header, sizeof(header));
        memcpy(send_buf + sizeof(header), buf, len);

        int sent = send(sta_sockfd, send_buf, sizeof(header) + len, MSG_DONTWAIT);
        dbg("STA id=%d  seq=%d   time=%u\n", header.client_id,header.seq_num,header.timestamp);
        if (sent < 0) {
            int err = errno;
            dbg("STA send to AP failed: %d\n", err);

            // 只在真正错误时才断开，EAGAIN 直接丢包
            if (err != EAGAIN && err != EWOULDBLOCK) {
                dbg("STA: TX fatal error, disconnect\n");
                sta_connected = false;
            }
        }
    }
    return len;
}


// ------------------------- STA 播放回调：根据是否被选中静音/播放 -------------------------
static uint32_t sta_playback_callback(uint8_t *buf, uint32_t len)
{
    bool current_selected;

    // 获取当前选中状态（线程安全）
    rtos_mutex_lock(sta_selected_mutex, -1);
    current_selected = sta_selected;
    rtos_mutex_unlock(sta_selected_mutex);

    if (!current_selected) {
        // 没被选中：静音
        memset(buf, 0, len);
    } else {
        // 被选中：播放最近收到的数据
        rtos_mutex_lock(sta_buffer_mutex, -1);
        memcpy(buf, sta_net_buf[sta_net_active_buf], len);
        rtos_mutex_unlock(sta_buffer_mutex);
    }

    return len;
}


// ----------------------------- STA 端 TCP 音频客户端任务 -----------------------------
void sta_tcp_client_task(void *arg)
{
    struct sockaddr_in server_addr = {0};
    int res;

    // 初始化互斥锁
    if (sta_buffer_mutex == NULL) {
        rtos_mutex_create(&sta_buffer_mutex);
    }
    if (sta_selected_mutex == NULL) {
        rtos_mutex_create(&sta_selected_mutex);
    }

    // 初始化 ASIO 音频系统
    if (!sta_asio_initialized) {
        asio_init();
        res = asio_open();
        if (res != ASIO_ERR_NONE) {
            dbg("STA ASIO open failed: %d\n", res);
            goto cleanup;
        }
        sta_asio_initialized = true;
        dbg("STA ASIO opened successfully\n");
    }

    // 配置 STA 采集流（发送到 AP）
    ASIO_STREAM_CFG_T sta_capture_cfg = {
        .path          = AUD_PATH_RX01,
        .device        = AUD_DEVICE_EXT_CODEC_I2S0,
        .device_role   = AUD_DEVICE_ROLE_MASTER,
        .bits          = AUD_BITS_24,
        .ch_num        = AUD_CH_NUM_2,
        .samp_rate     = AUD_SAMPRATE_16000,
        .src_samp_rate = AUD_SAMPRATE_48000,
        .buf_ptr       = sta_capture_buf,
        .buf_size      = AUDIO_BUF_SIZE,
        .handler       = sta_capture_callback,
        .src_en        = true
    };

    // 配置 STA 播放流（播放来自 AP 的音频）
    ASIO_STREAM_CFG_T sta_playback_cfg = {
        .path          = AUD_PATH_TX01,
        .device        = AUD_DEVICE_EXT_CODEC_I2S0,
        .device_role   = AUD_DEVICE_ROLE_MASTER,
        .bits          = AUD_BITS_24,
        .ch_num        = AUD_CH_NUM_2,
        .samp_rate     = AUD_SAMPRATE_16000,
        .src_samp_rate = AUD_SAMPRATE_48000,
        .buf_ptr       = sta_playback_buf,
        .buf_size      = AUDIO_BUF_SIZE,
        .handler       = sta_playback_callback,
        .src_en        = true
    };

    // 打开音频流（用 GROUP_1，避免和 AP 冲突）
    if ((res = asio_stream_open(AUD_STREAM_GROUP_1, AUD_STREAM_CAPTURE, &sta_capture_cfg)) != ASIO_ERR_NONE) {
        dbg("STA capture stream open failed: %d\n", res);
        goto cleanup;
    }

    if ((res = asio_stream_open(AUD_STREAM_GROUP_1, AUD_STREAM_PLAYBACK, &sta_playback_cfg)) != ASIO_ERR_NONE) {
        dbg("STA playback stream open failed: %d\n", res);
        goto close_capture;
    }

    // 启动音频流
    if ((res = asio_stream_start(AUD_STREAM_GROUP_1, AUD_STREAM_CAPTURE)) != ASIO_ERR_NONE) {
        dbg("STA capture start failed: %d\n", res);
        goto close_playback;
    }

    if ((res = asio_stream_start(AUD_STREAM_GROUP_1, AUD_STREAM_PLAYBACK)) != ASIO_ERR_NONE) {
        dbg("STA playback start failed: %d\n", res);
        goto stop_capture;
    }

    dbg("STA audio streams started successfully\n");

    // 主连接循环
    while (1) {
        // 创建 socket
        sta_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sta_sockfd < 0) {
            dbg("STA socket creation failed: %d\n", errno);
            rtos_task_suspend(1000);
            continue;
        }

        // 配置服务器地址（AP 的地址）
        server_addr.sin_family      = AF_INET;
        server_addr.sin_port        = htons(TCP_PORT);
        inet_aton(AP_IP, &server_addr.sin_addr);

        // 连接服务器
        if (connect(sta_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            dbg("STA connection to AP failed: %d\n", errno);
            close(sta_sockfd);
            sta_sockfd = -1;
            rtos_task_suspend(1000);
            continue;
        }

        // 设置 TCP_NODELAY 减少延迟
        int no_delay = 1;
        setsockopt(sta_sockfd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));

        sta_connected = true;
        dbg("STA connected to AP server\n");

        // 数据接收循环
        while (sta_connected) {
            audio_header_t header;
            uint8_t        recv_buf[sizeof(header) + AUDIO_BUF_SIZE];

            int len = recv(sta_sockfd, recv_buf, sizeof(recv_buf), 0);
            if (len <= 0) {
                dbg("STA connection lost: %d\n", errno);
                sta_connected = false;
                break;
            }
            uint64_t nowtime = get_us_time();
            dbg("nowtime = %u\n", nowtime);
            if (len >= (int)sizeof(header)) {
                memcpy(&header, recv_buf, sizeof(header));
                uint8_t *audio_data = recv_buf + sizeof(header);
                uint32_t audio_len  = len - sizeof(header);

                if (header.client_id == sta_client_id &&
                    header.direction == AUDIO_DIRECTION_AP_TO_STA) {

                    // 标记为被 AP 选中
                    rtos_mutex_lock(sta_selected_mutex, -1);
                    sta_selected = true;
                    rtos_mutex_unlock(sta_selected_mutex);

                    // 写入双缓冲
                    if (audio_len > AUDIO_BUF_SIZE) {
                        audio_len = AUDIO_BUF_SIZE;
                    }

                    rtos_mutex_lock(sta_buffer_mutex, -1);
                    int write_buffer = !sta_net_active_buf;
                    memcpy(sta_net_buf[write_buffer], audio_data, audio_len);
                    sta_net_active_buf = write_buffer;
                    rtos_mutex_unlock(sta_buffer_mutex);

                    // 调试：计算网络延迟
                    // uint64_t current_time = get_us_time();
                    // uint64_t latency      = current_time - header.timestamp;
                    // dbg("STA recv audio seq=%u latency=%llu us\n",
                    //     header.seq_num, latency);
                } else if (header.client_id != sta_client_id) {
                    // 不是发给当前 STA 的数据，标记为未选中
                    rtos_mutex_lock(sta_selected_mutex, -1);
                    sta_selected = false;
                    rtos_mutex_unlock(sta_selected_mutex);
                }
            }

            // 短暂休眠避免过度占用 CPU
            rtos_task_suspend(1);
        }

        close(sta_sockfd);
        sta_sockfd   = -1;
        sta_connected = false;
        dbg("STA disconnected, attempting reconnection...\n");
    }

stop_capture:
    asio_stream_stop(AUD_STREAM_GROUP_1, AUD_STREAM_CAPTURE);
close_playback:
    asio_stream_close(AUD_STREAM_GROUP_1, AUD_STREAM_PLAYBACK);
close_capture:
    asio_stream_close(AUD_STREAM_GROUP_1, AUD_STREAM_CAPTURE);
cleanup:
    asio_close();
    sta_asio_initialized = false;
    if (sta_sockfd >= 0) {
        close(sta_sockfd);
        sta_sockfd = -1;
    }
    rtos_task_delete(NULL);
}

#endif // TEST_CONNECT

// 设置目标STA ID的命令函数
int set_target_sta_cmd(int argc, char * const argv[]) {
    if (argc < 2) {
        dbg("Current target STA ID: %d\n", target_sta_id);
        return 0;
    }
    
    uint8_t new_id = atoi(argv[1]);
    if (new_id < 1 || new_id > 10) {
        dbg("Invalid STA ID, must be 1-%d\n", 10);
        return -1;
    }
    
    rtos_mutex_lock(target_id_mutex, -1);
    target_sta_id = new_id;
    rtos_mutex_unlock(target_id_mutex);
    
    dbg("Target STA ID set to: %d\n", target_sta_id);
    return 0;
}

//------------------------------2026-4-27---------------------------------------------
// static int at32_power_cmd(int argc, char * const argv[])
// {
//     (void)argc;
//     (void)argv;

//     /*
//      * 读取一次 AT32 电源状态。
//      *
//      * 正常应该打印：
//      * at32_i2c: id=0x42 bat=xx% bat_mv=xxxx usb=x charge=x level=x shutdown=x
//      */
//     dbg("at32pwr cmd enter\r\n");
//     app_at32_power_test_once();

//     return 0;
// }

//---------------------------------------------------------------------------------------------

///tcpsta
// TCP客户端任务

// TCP客户端任务
void tcp_client_task(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr = {0};
    char buffer[1460];
    //uint32_t packet_id = 0;
    int no_delay = 1;
    // 配置服务器地址 (AP的IP)
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    inet_aton("192.168.88.1", &server_addr.sin_addr); // AP固定IP

    while (1) {
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const int *)&no_delay, sizeof(no_delay)) != 0) {
            dbg("setsockopt failed\n");
            close(sockfd);
            sockfd = -1;
            return;
        }

        connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        
               // 发送数据
        uint32_t total_sent = 0;
        int64_t start_time = get_us_time();
        int64_t end_time = start_time + (TEST_DURATION_MS * 1000);
        
        while (get_us_time() < end_time) {
            int sent = send(sockfd, buffer, sizeof(buffer), 0);
            // dbg("sent=%d\r\n",sent);
            if (sent <= 0) {
                dbg("Send error: %d", errno);
                break;
            }
            total_sent += sent;
        }
    
    
    // 计算吞吐量
     // 计算速率
        //uint64_t duration_us = get_us_time() - start_time;
        //float rate_mbps = (total_sent * 8.0) / (duration_us / 1000000.0) / 1000000.0;
    // uint64_t elapsed_us = test_end - test_start;
    // float throughput = (TEST_PACKET_COUNT * sizeof(buffer) * 8.0) / (duration_us / 1000000.0);
    
    // dbg("Client: Loss: %.2f%%, test_end:%d test_start=%d elapsed_us=%d us, Throughput: %.2f Mbps\r\n",
    //     loss_rate, test_end, test_start, elapsed_us, throughput / 1000000);
        
    dbg("total_sent:%d\r\n",total_sent);

    // dbg("duration_us:%d\r\n",duration_us);
    
    // dbg("rate_mbps:%.2f Mbps\r\n", rate_mbps);
        
    // dbg("test_end:%d test_start=%d duration_us=%d us, rate_mbps: %.2f Mbps\r\n",
    //     end_time, start_time, duration_us, rate_mbps);


        
        close(sockfd);
        rtos_task_suspend(0); // 5秒后重新测试
    }
}



/////////////////////////////////////

///tcpap



// TCP服务器任务
void tcp_server_task(void *arg) {
    int server_fd, client_fd;
    int no_delay = 1;
    struct sockaddr_in server_addr = {0};
    char buffer[1460]; // MTU大小

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, (const int *)&no_delay, sizeof(no_delay)) != 0) {
            dbg("setsockopt failed\n");
            close(server_fd);
            server_fd = -1;
            return;
        }

    // 绑定监听
    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);

    while (1) {
        // 接受连接
        client_fd = accept(server_fd, NULL, NULL);
        uint64_t start = get_us_time();
        uint32_t total_bytes = 0;

        // struct timeval start, end;
        
        // gettimeofday(&start, NULL);
        

        // 数据接收回环
        while (1) {
            
            int len = recv(client_fd, buffer, sizeof(buffer), 0);
            if (len <= 0) break;
        
            // dbg("recv data len =%d\r\n",len);
            total_bytes += len;

            // send(client_fd, buffer, len, 0); // 立即回显
        }
        uint64_t end = get_us_time();
        uint64_t elapsed_us = end - start;


        if (elapsed_us == 0) elapsed_us = 1;
        //float throughput = (total_bytes * 8.0) / (elapsed_us / 1000000.0);
        // gettimeofday(&end, NULL);
        // 计算服务器端吞吐量 (基于文档9)
        // long elapsed_us = (end.tv_sec - start.tv_sec)*1000000 + (end.tv_usec - start.tv_usec);
        // float throughput = (total_bytes * 8.0) / (elapsed_us / 1000000.0); // bps
 
        
        // dbg("Server: Total RX: %u bytes, Throughput: %.2f Mbps,end:%d,start:%d,elapsed_us:%d\r\n", 
        //     total_bytes, throughput / 1000000,end,start,elapsed_us);
        dbg("Server: Total RX: %u bytes\r\n", 
            total_bytes);
        
        close(client_fd);
    }
}



//tcp
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//udp

// AP模式: UDP服务器任务
void udp_server_task(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    udp_packet_t packet;
    int recv_len;
    
    // 创建UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        dbg("AP: socket creation failed\n");
        return;
    }
    
    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(AP_IP);
    
    // 绑定socket
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        dbg("AP: bind failed\n");
        close(sockfd);
        return;
    }
    
    dbg("AP: UDP server started on %s:%d\n", AP_IP, UDP_PORT);
    
    // 初始化统计
    stats.total_packets = 0;
    stats.total_bytes = 0;
    stats.start_time = get_us_time();
    stats.last_report_time = stats.start_time;
    
    while (1) {
        // 接收数据
        recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, 
                          (struct sockaddr*)&client_addr, &addr_len);
        
        if (recv_len > 0) {
            // 更新统计
            stats.total_packets++;
            stats.total_bytes += recv_len;
            
            // 计算当前时间
            uint64_t current_time = get_us_time();
            
            // 定期打印统计信息
            if (current_time - stats.last_report_time >= STATS_INTERVAL * 1000) {
                float elapsed_sec = (current_time - stats.last_report_time) / 1000000.0f;
                float rate_mbps = (stats.total_bytes * 8.0f) / elapsed_sec / 1000000.0f;
                
                dbg("AP: Received %d packets, %.2f Mbps\n", 
                    stats.total_packets, rate_mbps);
                
                // 重置统计
                stats.total_packets = 0;
                stats.total_bytes = 0;
                stats.last_report_time = current_time;
            }
        }
    }
    
    close(sockfd);
}


// STA模式: UDP客户端任务
void udp_client_task(void *arg) {
    int sockfd;
    struct sockaddr_in server_addr;
    udp_packet_t packet;
    uint32_t packet_count = 0;
    uint64_t last_send_time = 0;
    uint64_t interval_us = 1000000 / PACKETS_PER_SECOND; // 包间隔(微秒)
    
    // 创建UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        dbg("STA: socket creation failed\n");
        return;
    }
    
    // 设置服务器地址(AP的地址)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(AP_IP);
    
    dbg("STA: UDP client connected to %s:%d\n", AP_IP, UDP_PORT);
    
    // 初始化统计
    stats.total_packets = 0;
    stats.total_bytes = 0;
    stats.start_time = get_us_time();
    stats.last_report_time = stats.start_time;
    
    while (1) {
        uint64_t current_time = get_us_time();
        
        // 检查是否到达发送时间
        if (current_time - last_send_time >= interval_us) {
            // 准备数据包
            packet.seq_num = packet_count++;
            packet.timestamp_us = current_time;
            // 这里可以填充真实音频数据，现在用模拟数据
            memset(packet.data, 0xAA, sizeof(packet.data));
            
            // 发送数据
            sendto(sockfd, &packet, sizeof(packet), 0, 
                  (struct sockaddr*)&server_addr, sizeof(server_addr));
            
            // 更新统计
            stats.total_packets++;
            stats.total_bytes += sizeof(packet);
            last_send_time = current_time;
            
            // 定期打印统计信息
            if (current_time - stats.last_report_time >= STATS_INTERVAL * 1000) {
                float elapsed_sec = (current_time - stats.last_report_time) / 1000000.0f;
                float rate_mbps = (stats.total_bytes * 8.0f) / elapsed_sec / 1000000.0f;
                
                dbg("STA: Sent %d packets, %.2f Mbps\n", 
                    stats.total_packets, rate_mbps);
                
                // 重置统计
                stats.total_packets = 0;
                stats.total_bytes = 0;
                stats.last_report_time = current_time;
            }
        }
        
        // 短暂休眠以节省CPU
        rtos_task_suspend(1);
    }
    
    close(sockfd);
}





//////////////////////////////////////////////////////////////////////////////////

void gpio_test_task(void)
{
    int state = 0;
    dbg("gpio test start pin=%d.....\r\n",GPIO_TEST_PIN);



    while(1){
        if(wlan_connected)
        {
            state = !state;
            if(state)
            {
                gpioa_set(GPIO_TEST_PIN);
            }
            else
            {
                gpioa_clr(GPIO_TEST_PIN);
            }
        }
        else
        {

            rtos_task_delete(NULL);
            gpioa_clr(GPIO_TEST_PIN);
        }

      

        rtos_task_suspend(GPIO_TEST_DELAY_MS);

        
    }
    dbg("test done");
}



/**
 * Functions
 */

void demo_src_main(void)
{
     dbg("Demo src\n");
}


/**
 * Show system clocks
 */
int do_show_clocks(int argc, char * const argv[])
{
    #if (PLF_AIC8800)
    dbg("F:%dM,H:%dM,P:%dM,FL:%dM\n",
               DSPSysCoreClock/1000000,SystemCoreClock/1000000,PeripheralClock/1000000,sysctrl_clock_get(PER_FLASH)/1000000);
    #else
    dbg("H:%dM,P:%dM,PSR:%dM,FL:%dM\n",
        SystemCoreClock/1000000,PeripheralClock/1000000,FlashMem2XClock/1000000,sysctrl_clock_get(PER_FLASH)/1000000);
    #endif
    return 0;
}





#if (TEST_SCAN)
int time_interval = 2000; //ms
uint8_t chan_cnt_saved_2g4 = 0, chan_cnt_saved_5g = 0;
extern struct me_chan_config_req fhost_chan;

int do_set_scanparam(int argc, char * const argv[])
{
    int ret = 0;
    int band_type = (int)console_cmd_strtoul(argv[1], NULL, 10);
    int time_interv = (int)console_cmd_strtoul(argv[2], NULL, 10);
    time_interval = time_interv;
    if (band_type == 0) { // stop
    } else if (band_type == 1) { // 2.4g
        dbg("scan @ 2.4G band, interval=%d\n", time_interval);
        fhost_chan.chan2G4_cnt = chan_cnt_saved_2g4;
        fhost_chan.chan5G_cnt = 0;
    } else if (band_type == 2) { // 5g
        dbg("scan @ 5G band, interval=%d\n", time_interval);
        fhost_chan.chan2G4_cnt = 0;
        fhost_chan.chan5G_cnt = chan_cnt_saved_5g;
    } else if (band_type == 3) { // 2.4g+5g
        dbg("scan @ 2.4G+5G band, interval=%d\n", time_interval);
        fhost_chan.chan2G4_cnt = chan_cnt_saved_2g4;
        fhost_chan.chan5G_cnt = chan_cnt_saved_5g;
    } else {
        dbg("invalid band_type: %d\n", band_type);
    }
    return ret;
}
#endif


static RTOS_TASK_FCT(fhost_example_task)
{
#if TEST_SOFTAP   //把设备做成AP端
    uint8_t band = 1;       // 0: 2.4G, 1: 5G
    char *ssid = "aic8800m40";
    char *pw = "12345678";    // NULL for open
    #if (CONFIG_AUTO_PING)
    char *ping_params =
        #if PLF_HW_PXP
        "-r 50 "
        #endif
        "192.168.66.100";
    #endif

    //is_ap = 1;
    do {
        
        #if 1
        int ret;
        set_ap_enable_he_rate(1);
        set_ap_enable_ht_40(0);
        wlan_ap_switch_channel(165); 
        ret = wlan_start_ap(band, (uint8_t *)ssid, (uint8_t *)pw);
        // set_ap_enable_he_rate(1);
        // set_ap_enable_ht_40(1);
        
        if (ret) {
            dbg("AP start err: %d\n", ret);
            break;
        }   
        
        dbg("AP start !\n");

        
        
        #if UDPSelect
        //创建UDP_Server
        dbg("Creating TCP server task...\n");
        int aa = rtos_task_create(udp_server_task, "UDP_Server", 0, 1024, NULL, 7, NULL);
        dbg("Task creation result: %d\n", aa);
        #endif

        #if TCPSelect
        dbg("Creating TCP server task...\n");
        // int aa = rtos_task_create(tcp_server_task, "TCP_Server", UNDEF_TASK, 1024, NULL, 5, NULL);
        int aa = rtos_task_create(ap_tcp_server_task, "Audio TX", 
                     APPLICATION_TASK, 4096, NULL, 5, NULL);
    
            
        // int aa = rtos_task_create(tcp_audio_transfer_task, "Audio TX", 
        //              APPLICATION_TASK, 4096, NULL, 5, NULL);
        dbg("Task creation result: %d\n", aa);
        #endif

        #endif




    //         // 设置 AP 固定 IP
    // ip4_addr_t ipaddr, netmask, gw;
    // IP4_ADDR(&ipaddr, 192, 168, 88, 1);
    // IP4_ADDR(&netmask, 255, 255, 255, 0);
    // IP4_ADDR(&gw, 192, 168, 88, 1);
    // netif_set_addr(&rndis_netif, &ipaddr, &netmask, &gw);
        // udp_recv();

        #if (CONFIG_AUTO_PING)
        while (!ps_sta_connected) {
            #if (PLF_HW_PXP == 1)
            rtos_task_suspend(5);
            #else /* PLF_HW_PXP */
            rtos_task_suspend(200);
            #endif /* PLF_HW_PXP */
        }
        dbg("ps STA found, do ping\n");

        ret = fhost_console_ping(ping_params);
        if (ret) {
            dbg("ping err: %d\n", ret);
            break;
        }
        #endif
    } while (0);
#elif TEST_CONNECT
    if (!wlan_connected) {
        char *ssid = "aic8800m40", *pw = "12345678";

        #if PLF_HW_PXP
        rtos_task_suspend(5);   // wait for AP starting
        #endif /* PLF_HW_PXP */  

        if (0 == wlan_start_sta((uint8_t *)ssid, (uint8_t *)pw, 0)) {
            wlan_connected = 1;
            dbg("connected %s\r\n",ssid);
            
            #if 0
            IP4_ADDR(&remote_ip.u_addr.ip4, 192,168,88,1);
            #endif


            // if(gpioflash == NULL)
            // {
            //     rtos_task_create((rtos_task_fct)gpio_test_task, "GPIO_Task", 11,256, NULL, 7, &gpioflash);
            // }

            #if UDPSelect
            //创建UDP_Client
           
            rtos_task_create(udp_client_task, "UDP_Client", 0, 1024, NULL, 5, NULL);
            #endif
            // while(1)
            // {
            //     test_time_func();
            // }
            #if TCPSelect
            dbg("Creating TCP server task...\n");
            // int bb=rtos_task_create(tcp_client_task, "TCP_Client", UNDEF_TASK, 1024, NULL, 5, NULL);
            int bb=rtos_task_create(sta_tcp_client_task, "Audio RX", 
                     APPLICATION_TASK, 4096, NULL, 5, NULL);
            dbg("sta_id=%u",sta_client_id);
            dbg("Task creation result: %d\n", bb);
            #endif

           
        }
        else
        {
            wlan_connected = 0;
            gpioa_clr(GPIO_TEST_PIN);
            dbg("disconnected!! %s\r\n",ssid);
        }


        #if CONFIG_SLEEP_LEVEL == 1
        sleep_level_set(PM_LEVEL_LIGHT_SLEEP);
        #elif CONFIG_SLEEP_LEVEL == 2
        sleep_level_set(PM_LEVEL_DEEP_SLEEP);
        #elif CONFIG_SLEEP_LEVEL == 3
        sleep_level_set(PM_LEVEL_HIBERNATE);
        #endif
        user_sleep_allow(1);
        #if !(PLF_HW_PXP)
        // Get quote from a website
        // example_connect_website();
        #endif /* !PLF_HW_PXP */
    }
    #if (PLF_AIC8800)
    else {
        fhost_sta_recover_connection();
    }
    #endif
#elif TEST_SCAN
    int nb_res;
    struct mac_scan_result result;
    struct fhost_vif_status fvif_status;
    unsigned int fvif_idx = 0;

    ipc_host_cntrl_start();

    cntrl_link = fhost_cntrl_cfgrwnx_link_open();
    if (cntrl_link == NULL) {
        dbg(D_ERR "Failed to open link with control task\n");
        ASSERT_ERR(0);
    }
    // Reset STA interface if needed (this may end previous wpa_supplicant task)
    // 1. 获取当前虚拟接口状态
    if (fhost_get_vif_status(fvif_idx, &fvif_status) ||
    // 2. 若接口未初始化或类型未知，强制设置为STA模式
        ((fvif_status.type == VIF_UNKNOWN) &&
         fhost_set_vif_type(cntrl_link, fvif_idx, VIF_STA, false))) {
             // 3. 若上述操作失败，立即关闭控制链路并退出
        fhost_cntrl_cfgrwnx_link_close(cntrl_link);
        return;
    }

    for (;;) {
        rtos_task_suspend(time_interval);
        nb_res = fhost_scan(cntrl_link, fvif_idx, NULL);
        dbg("Got %d scan results\n", nb_res);

        nb_res = 0;
        while(fhost_get_scan_results(cntrl_link, nb_res++, 1, &result)) {
            result.ssid.array[result.ssid.length] = '\0'; // set ssid string ending
            dbg("(%3d dBm) CH=%3d BSSID=%02x:%02x:%02x:%02x:%02x:%02x "
                #if (PLF_AIC8800)
                "Format %x "
                #endif
                "SSID=%s\n",
                (int8_t)result.rssi, phy_freq_to_channel(result.chan->band, result.chan->freq),
                ((uint8_t *)result.bssid.array)[0], ((uint8_t *)result.bssid.array)[1],
                ((uint8_t *)result.bssid.array)[2], ((uint8_t *)result.bssid.array)[3],
                ((uint8_t *)result.bssid.array)[4], ((uint8_t *)result.bssid.array)[5],
                #if (PLF_AIC8800)
                result.format,
                #endif
                (char *)result.ssid.array);
        }
    }
    if (fvif_status.type == VIF_UNKNOWN) {
        fhost_set_vif_type(cntrl_link, fvif_idx, VIF_UNKNOWN, false);
    }
    fhost_cntrl_cfgrwnx_link_close(cntrl_link);

    // while (1);
#elif TEST_MONITOR
    do {
        struct fhost_vif_monitor_cfg cfg;
        struct mac_chan_def *chan;
        unsigned int fvif_idx = 0;
        ipc_host_cntrl_start();
        #if (PLF_AIC8800)
        fhost_cntrl_mm_set_filter(0X3503848C);
        #endif
        cntrl_link = fhost_cntrl_cfgrwnx_link_open();
        if (cntrl_link == NULL) {
            dbg(D_ERR "Failed to open link with control task\n");
            ASSERT_ERR(0);
        }
        // frequency
        cfg.chan.prim20_freq = 2437; // ch 6
        chan = fhost_chan_get(cfg.chan.prim20_freq);
        if (chan == NULL) {
            dbg("Invalid freq %d\n", cfg.chan.prim20_freq);
            break;
        }
        cfg.chan.band = chan->band;
        cfg.chan.tx_power = chan->tx_power;
        // by default 20Mhz bandwidth
        cfg.chan.type = PHY_CHNL_BW_20;
        cfg.chan.center1_freq = cfg.chan.prim20_freq;
        cfg.chan.center2_freq = 0;
        if (fhost_set_vif_type(cntrl_link, fvif_idx, VIF_MONITOR, false)) {
            dbg("Error while enabling monitor mode\n");
            break;
        }
        cfg.uf = true;
        cfg.cb = example_monitor_callback;
        cfg.cb_arg = NULL;
        if (fhost_cntrl_monitor_cfg(cntrl_link, fvif_idx, &cfg)) {
            dbg("Error while configuring monitor mode\n");
            break;
        }
        // wait 10s
        rtos_task_suspend(10000);
        // stop monitor
        if (fhost_set_vif_type(cntrl_link, fvif_idx, VIF_UNKNOWN, false)) {
            dbg("Error while disabling monitor mode\n");
            break;
        }
    } while (0);
#else
#error "invalid test"
#endif

    rtos_task_delete(NULL);
}



int example_init(void)
{
    // data_rate_parsing_config(1);
    if (rtos_task_create(fhost_example_task, "Example task", APPLICATION_TASK,
                         1024, NULL, RTOS_TASK_PRIORITY(1), NULL))
        return 1;
   
    
    console_cmd_add("target_sta", "<id> - Set target STA ID", 1, 2, set_target_sta_cmd);
    if (target_id_mutex == NULL) {
        rtos_mutex_create(&target_id_mutex);
    }   

        
    #if 0
    //初始化UDP
    upcb = udp_new();
    if(upcb == NULL)
    {
        dbg("Failed to create UDP PCB\r\n");
        return 1;
    }

    //设置接受回调
    udp_recv(upcb,udp_server_recv_cb,NULL);

    // 绑定本地端口
    if (udp_bind(upcb, IP_ADDR_ANY, local_port) != ERR_OK) {
        dbg("Failed to bind UDP port\n");
        udp_remove(upcb);
        return 1;
    }

    dbg("UDP initialized on port %d\n", local_port);

    #endif


    console_cmd_add("clk", "- show cur clk",         1, 1, do_show_clocks);

    #if (TEST_SCAN)
    chan_cnt_saved_2g4 = fhost_chan.chan2G4_cnt;
    chan_cnt_saved_5g  = fhost_chan.chan5G_cnt;
    console_cmd_add("scanparam", "band interval - set scan params", 3, 3, do_set_scanparam);
    #endif

    //-----------------2026-4-23------------------------------------------------------
    // app_aic_power_adc_init();
    // app_aic_power_adc_console_init();
   
    //-----------------------------------------------------------------------------------
    return 0;
}



/*
 * Entry point of hostif_rxdata application
 */
static RTOS_TASK_FCT(user_code_task)
{
    // wait ble ready
    #if PLF_BT_STACK
    #if PLF_BLE_ONLY
    int ble_ready = 1;
    extern uint8_t ble_user_init_status_get(void);
    while(ble_user_init_status_get() != ble_ready) {
        rtos_task_suspend(10);
    }
    #endif
    #endif

    // wait wifi ready
    #if PLF_WIFI_STACK
    #if (PLF_AIC8800)
    int wifi_ready = 0;
    #elif (PLF_AIC8800MC) || (PLF_AIC8800M40)
    int wifi_ready = IPC_EMB_START;
    #endif
    while(wifi_get_init_status() != wifi_ready) {
        rtos_task_suspend(10);
    };
    #endif
    int ret;

    // user can initialize peripherals and create tasks safely.
    // Such as
    // demo_led_init();
    // demo_key_init();
    
    gpioa_init(GPIO_TEST_PIN);
    gpioa_dir_out(GPIO_TEST_PIN);
    // asio_init();
    // rtos_task_create((rtos_task_fct)gpio_test_task, "GPIO_Task", 11,256, NULL, 1, NULL);
    us_ticker_init();
    
    /*新增wifi连接任务 */
    ret = example_init();
    
    if(ret)
    {
        dbg("example_init failed \r\n");
    }
    else if(0 == ret)
    {
        dbg("example_init successfull \r\n");
    }

 

    // ...
    // demo_task_create();

    // demo_src_main();
    // demo_lib_main();

    // delete this task later.
    rtos_task_delete(NULL);
}
void user_code_entry(void)
{
   
    
 
    if (rtos_task_create(user_code_task, "user code", USER_CODE_TASK,
                         512, NULL, TASK_PRIORITY_USER_CODE, NULL)) {
        dbg("user_code_enter create failed\n");
        return ;
    }
}