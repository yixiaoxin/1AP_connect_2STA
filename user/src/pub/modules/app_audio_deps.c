/*
 * app_audio_deps.c
 *
 * v7.0.2 build-mode-safe audio dependency bridge.
 *
 * target_wifi/build_wifi_case_8800m40.sh does not add the SDK ASIO/audio
 * source objects, so include the required implementation files here.
 *
 * target_btdm_wifi2 with ASIO=on defines CFG_ASIO and already compiles those
 * SDK source files.  In that build this translation unit must stay empty to
 * avoid duplicate symbols and duplicate BSS.
 */

#if defined(CFG_ASIO)

void app_audio_deps_sdk_asio_already_linked(void)
{
    /* SDK build system owns ASIO/audio_proc/codec in this configuration. */
}

#else

#include "../../../../audio/device/codec_tlv320aic32.c"

/* asio.c uses implementation provided by asio_src.c. */
#include "../../../../audio/asio/asio_src.c"
#include "../../../../audio/asio/asio.c"

#include "../../../../audio/process/audio_proc/audio_proc_api.c"

#endif
