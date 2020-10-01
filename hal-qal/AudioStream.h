/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ANDROID_HARDWARE_AHAL_ASTREAM_H_
#define ANDROID_HARDWARE_AHAL_ASTREAM_H_

#include <stdlib.h>
#include <unistd.h>

#include <vector>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <system/audio.h>

#include "QalDefs.h"
#include <audio_extn/AudioExtn.h>
#include <mutex>
#include <map>

#ifdef LINUX_ENABLED
#include <condition_variable>
#endif

#define LOW_LATENCY_PLATFORM_DELAY (13*1000LL)
#define DEEP_BUFFER_PLATFORM_DELAY (29*1000LL)
#define PCM_OFFLOAD_PLATFORM_DELAY (30*1000LL)
#define MMAP_PLATFORM_DELAY        (3*1000LL)
#define ULL_PLATFORM_DELAY         (4*1000LL)

#define DEEP_BUFFER_OUTPUT_PERIOD_DURATION 40
#define PCM_OFFLOAD_OUTPUT_PERIOD_DURATION 80
#define LOW_LATENCY_OUTPUT_PERIOD_DURATION 5
#define VOIP_PERIOD_COUNT_DEFAULT 2
#define DEFAULT_VOIP_BUF_DURATION_MS 20
#define DEFAULT_VOIP_BIT_DEPTH_BYTE sizeof(int16_t)
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY  50

#define DEFAULT_OUTPUT_SAMPLING_RATE    48000
#define LOW_LATENCY_PLAYBACK_PERIOD_SIZE 240 /** 5ms; frames */
#define LOW_LATENCY_PLAYBACK_PERIOD_COUNT 2

#define PCM_OFFLOAD_PLAYBACK_PERIOD_COUNT 2 /** Direct PCM */
#define DEEP_BUFFER_PLAYBACK_PERIOD_COUNT 2 /** Deep Buffer*/

#define ULL_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE / 1000) /** 1ms; frames */
#define ULL_PERIOD_COUNT_DEFAULT 512
#define ULL_PERIOD_MULTIPLIER 3
#define BUF_SIZE_PLAYBACK 1024
#define BUF_SIZE_CAPTURE 960
#define NO_OF_BUF 4
#define LOW_LATENCY_CAPTURE_SAMPLE_RATE 48000
#define LOW_LATENCY_CAPTURE_PERIOD_SIZE 240
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#define LOW_LATENCY_CAPTURE_USE_CASE 1
#define MIN_PCM_FRAGMENT_SIZE 512
#define MAX_PCM_FRAGMENT_SIZE (240 * 1024)
#define MMAP_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define MMAP_PERIOD_COUNT_MIN 32
#define MMAP_PERIOD_COUNT_MAX 512
#define MMAP_PERIOD_COUNT_DEFAULT (MMAP_PERIOD_COUNT_MAX)
#define CODEC_BACKEND_DEFAULT_BIT_WIDTH 16
#define AUDIO_CAPTURE_PERIOD_DURATION_MSEC 20

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

#if LINUX_ENABLED
#if defined(__LP64__)
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib64/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/usr/lib64/libqcomvisualizer.so"
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/usr/lib/libqcomvisualizer.so"
#endif
#else
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#define VISUALIZER_LIBRARY_PATH "/vendor/lib/soundfx/libqcomvisualizer.so"
#endif

/* These are the supported use cases by the hardware.
 * Each usecase is mapped to a specific PCM device.
 * Refer to pcm_device_table[].
 */
enum {
#if !LINUX_ENABLED
    USECASE_INVALID = -1,
#endif
    /* Playback usecases */
    USECASE_AUDIO_PLAYBACK_DEEP_BUFFER = 0,
    USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    USECASE_AUDIO_PLAYBACK_MULTI_CH,
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    USECASE_AUDIO_PLAYBACK_OFFLOAD4,
    USECASE_AUDIO_PLAYBACK_OFFLOAD5,
    USECASE_AUDIO_PLAYBACK_OFFLOAD6,
    USECASE_AUDIO_PLAYBACK_OFFLOAD7,
    USECASE_AUDIO_PLAYBACK_OFFLOAD8,
    USECASE_AUDIO_PLAYBACK_OFFLOAD9,
    USECASE_AUDIO_PLAYBACK_ULL,
    USECASE_AUDIO_PLAYBACK_MMAP,
    USECASE_AUDIO_PLAYBACK_WITH_HAPTICS,
    USECASE_AUDIO_PLAYBACK_HIFI,
    USECASE_AUDIO_PLAYBACK_TTS,

    /* FM usecase */
    USECASE_AUDIO_PLAYBACK_FM,

    /* HFP Use case*/
    USECASE_AUDIO_HFP_SCO,
    USECASE_AUDIO_HFP_SCO_WB,

    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_COMPRESS,
    USECASE_AUDIO_RECORD_COMPRESS2,
    USECASE_AUDIO_RECORD_COMPRESS3,
    USECASE_AUDIO_RECORD_COMPRESS4,
    USECASE_AUDIO_RECORD_COMPRESS5,
    USECASE_AUDIO_RECORD_COMPRESS6,
    USECASE_AUDIO_RECORD_LOW_LATENCY,
    USECASE_AUDIO_RECORD_FM_VIRTUAL,
    USECASE_AUDIO_RECORD_HIFI,

    USECASE_AUDIO_PLAYBACK_VOIP,
    USECASE_AUDIO_RECORD_VOIP,
    /* Voice usecase */
    USECASE_VOICE_CALL,
    USECASE_AUDIO_RECORD_MMAP,

    /* Voice extension usecases */
    USECASE_VOICE2_CALL,
    USECASE_VOLTE_CALL,
    USECASE_QCHAT_CALL,
    USECASE_VOWLAN_CALL,
    USECASE_VOICEMMODE1_CALL,
    USECASE_VOICEMMODE2_CALL,
    USECASE_COMPRESS_VOIP_CALL,

    USECASE_INCALL_REC_UPLINK,
    USECASE_INCALL_REC_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_COMPRESS,
    USECASE_INCALL_REC_DOWNLINK_COMPRESS,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS,

    USECASE_INCALL_MUSIC_UPLINK,
    USECASE_INCALL_MUSIC_UPLINK2,

    USECASE_AUDIO_SPKR_CALIB_RX,
    USECASE_AUDIO_SPKR_CALIB_TX,

    USECASE_AUDIO_PLAYBACK_AFE_PROXY,
    USECASE_AUDIO_RECORD_AFE_PROXY,
    USECASE_AUDIO_DSM_FEEDBACK,

    USECASE_AUDIO_PLAYBACK_SILENCE,

    USECASE_AUDIO_TRANSCODE_LOOPBACK_RX,
    USECASE_AUDIO_TRANSCODE_LOOPBACK_TX,

    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8,

    USECASE_AUDIO_EC_REF_LOOPBACK,

    USECASE_AUDIO_A2DP_ABR_FEEDBACK,

    /* car streams usecases */
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
    USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE,
    USECASE_AUDIO_PLAYBACK_PHONE,

    /*Audio FM Tuner usecase*/
    USECASE_AUDIO_FM_TUNER_EXT,
    AUDIO_USECASE_MAX
};

struct string_to_enum {
    const char *name;
    uint32_t value;
};
#define STRING_TO_ENUM(string) { #string, string }

static const struct string_to_enum formats_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_16_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_24_BIT_PACKED),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_32_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3_JOC),
    STRING_TO_ENUM(AUDIO_FORMAT_DOLBY_TRUEHD),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS_HD),
    STRING_TO_ENUM(AUDIO_FORMAT_IEC61937)
};

static const struct string_to_enum channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_2POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_QUAD),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_SURROUND),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_PENTA),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_6POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_MONO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_FRONT_BACK),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_1),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_2),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_3),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_4),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_5),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_6),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_7),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_8),
};

const std::map<uint32_t, qal_audio_fmt_t> getFormatId {
    {AUDIO_FORMAT_PCM,                 QAL_AUDIO_FMT_DEFAULT_PCM},
    {AUDIO_FORMAT_MP3,                 QAL_AUDIO_FMT_MP3},
    {AUDIO_FORMAT_AAC,                 QAL_AUDIO_FMT_AAC},
    {AUDIO_FORMAT_AAC_ADTS,            QAL_AUDIO_FMT_AAC_ADTS},
    {AUDIO_FORMAT_AAC_ADIF,            QAL_AUDIO_FMT_AAC_ADIF},
    {AUDIO_FORMAT_AAC_LATM,            QAL_AUDIO_FMT_AAC_LATM},
    {AUDIO_FORMAT_WMA,                 QAL_AUDIO_FMT_WMA_STD},
    {AUDIO_FORMAT_ALAC,                QAL_AUDIO_FMT_ALAC},
    {AUDIO_FORMAT_APE,                 QAL_AUDIO_FMT_APE},
    {AUDIO_FORMAT_WMA_PRO,             QAL_AUDIO_FMT_WMA_PRO},
    {AUDIO_FORMAT_FLAC,                QAL_AUDIO_FMT_FLAC},
    {AUDIO_FORMAT_VORBIS,              QAL_AUDIO_FMT_VORBIS}
};

const uint32_t format_to_bitwidth_table[] = {
    [AUDIO_FORMAT_DEFAULT] = 0,
    [AUDIO_FORMAT_PCM_16_BIT] = 16,
    [AUDIO_FORMAT_PCM_8_BIT] = 8,
    [AUDIO_FORMAT_PCM_32_BIT] = 32,
    [AUDIO_FORMAT_PCM_8_24_BIT] = 32,
    [AUDIO_FORMAT_PCM_FLOAT] = sizeof(float) * 8,
    [AUDIO_FORMAT_PCM_24_BIT_PACKED] = 24,
};

const char * const use_case_table[AUDIO_USECASE_MAX] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = "deep-buffer-playback",
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = "low-latency-playback",
    [USECASE_AUDIO_PLAYBACK_MULTI_CH]    = "multi-channel-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] = "compress-offload-playback",
    //Enabled for Direct_PCM
    [USECASE_AUDIO_PLAYBACK_OFFLOAD2] = "compress-offload-playback2",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD3] = "compress-offload-playback3",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD4] = "compress-offload-playback4",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD5] = "compress-offload-playback5",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD6] = "compress-offload-playback6",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD7] = "compress-offload-playback7",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD8] = "compress-offload-playback8",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD9] = "compress-offload-playback9",
    [USECASE_AUDIO_PLAYBACK_ULL]         = "audio-ull-playback",
    [USECASE_AUDIO_PLAYBACK_MMAP] = "mmap-playback",
    [USECASE_AUDIO_PLAYBACK_WITH_HAPTICS] = "audio-with-haptics-playback",
    [USECASE_AUDIO_PLAYBACK_HIFI] = "hifi-playback",
    [USECASE_AUDIO_PLAYBACK_TTS] = "audio-tts-playback",

    [USECASE_AUDIO_PLAYBACK_FM] = "play-fm",

    [USECASE_AUDIO_HFP_SCO] = "hfp-sco",
    [USECASE_AUDIO_HFP_SCO_WB] = "hfp-sco-wb",

    [USECASE_AUDIO_RECORD] = "audio-record",
    [USECASE_AUDIO_RECORD_COMPRESS] = "audio-record-compress",
    [USECASE_AUDIO_RECORD_COMPRESS2] = "audio-record-compress2",
    [USECASE_AUDIO_RECORD_COMPRESS3] = "audio-record-compress3",
    [USECASE_AUDIO_RECORD_COMPRESS4] = "audio-record-compress4",
    [USECASE_AUDIO_RECORD_COMPRESS5] = "audio-record-compress5",
    [USECASE_AUDIO_RECORD_COMPRESS6] = "audio-record-compress6",
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = "low-latency-record",
    [USECASE_AUDIO_RECORD_FM_VIRTUAL] = "fm-virtual-record",
    [USECASE_AUDIO_RECORD_HIFI] = "hifi-record",

    [USECASE_AUDIO_PLAYBACK_VOIP] = "audio-playback-voip",
    [USECASE_AUDIO_RECORD_VOIP] = "audio-record-voip",

    [USECASE_VOICE_CALL] = "voice-call",
    [USECASE_AUDIO_RECORD_MMAP] = "mmap-record",

    [USECASE_VOICE2_CALL] = "voice2-call",
    [USECASE_VOLTE_CALL] = "volte-call",
    [USECASE_QCHAT_CALL] = "qchat-call",
    [USECASE_VOWLAN_CALL] = "vowlan-call",
    [USECASE_VOICEMMODE1_CALL] = "voicemmode1-call",
    [USECASE_VOICEMMODE2_CALL] = "voicemmode2-call",
    [USECASE_COMPRESS_VOIP_CALL] = "compress-voip-call",
    [USECASE_INCALL_REC_UPLINK] = "incall-rec-uplink",
    [USECASE_INCALL_REC_DOWNLINK] = "incall-rec-downlink",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = "incall-rec-uplink-and-downlink",
    [USECASE_INCALL_REC_UPLINK_COMPRESS] = "incall-rec-uplink-compress",
    [USECASE_INCALL_REC_DOWNLINK_COMPRESS] = "incall-rec-downlink-compress",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS] = "incall-rec-uplink-and-downlink-compress",

    [USECASE_INCALL_MUSIC_UPLINK] = "incall_music_uplink",
    [USECASE_INCALL_MUSIC_UPLINK2] = "incall_music_uplink2",
    [USECASE_AUDIO_SPKR_CALIB_RX] = "spkr-rx-calib",
    [USECASE_AUDIO_SPKR_CALIB_TX] = "spkr-vi-record",

    [USECASE_AUDIO_PLAYBACK_AFE_PROXY] = "afe-proxy-playback",
    [USECASE_AUDIO_RECORD_AFE_PROXY] = "afe-proxy-record",
    [USECASE_AUDIO_DSM_FEEDBACK] = "dsm-silence",
    [USECASE_AUDIO_PLAYBACK_SILENCE] = "silence-playback",

    /* Transcode loopback cases */
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_RX] = "audio-transcode-loopback-rx",
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_TX] = "audio-transcode-loopback-tx",

    /* For Interactive Audio Streams */
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1] = "audio-interactive-stream1",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2] = "audio-interactive-stream2",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3] = "audio-interactive-stream3",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4] = "audio-interactive-stream4",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5] = "audio-interactive-stream5",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6] = "audio-interactive-stream6",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7] = "audio-interactive-stream7",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8] = "audio-interactive-stream8",

    [USECASE_AUDIO_EC_REF_LOOPBACK] = "ec-ref-audio-capture",

    [USECASE_AUDIO_A2DP_ABR_FEEDBACK] = "a2dp-abr-feedback",

    [USECASE_AUDIO_PLAYBACK_MEDIA] = "media-playback",
    [USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION] = "sys-notification-playback",
    [USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE] = "nav-guidance-playback",
    [USECASE_AUDIO_PLAYBACK_PHONE] = "phone-playback",
    [USECASE_AUDIO_FM_TUNER_EXT] = "fm-tuner-ext",
};

extern "C" typedef void (*hello_t)( const char* text );
extern "C" typedef int (*offload_effects_start_output)(audio_io_handle_t,
                                                       qal_stream_handle_t*);
extern "C" typedef int (*offload_effects_stop_output)(audio_io_handle_t,
                                                      qal_stream_handle_t*);

extern "C" typedef int (*visualizer_hal_start_output)(audio_io_handle_t,
                                                       qal_stream_handle_t*);
extern "C" typedef int (*visualizer_hal_stop_output)(audio_io_handle_t,
                                                      qal_stream_handle_t*);

int adev_open(audio_hw_device_t **device);

class AudioDevice;

class StreamPrimary {
public:
    StreamPrimary(audio_io_handle_t handle,
        audio_devices_t devices,
        struct audio_config *config);
    ~StreamPrimary();
    uint32_t        GetSampleRate();
    uint32_t        GetBufferSize();
    audio_format_t  GetFormat();
    uint32_t        GetChannelMask();
    int getQalDeviceIds(const audio_devices_t halDeviceId, qal_device_id_t* qalOutDeviceIds);
    audio_io_handle_t GetHandle();
    int             GetUseCase();
    std::mutex write_wait_mutex_;
    std::condition_variable write_condition_;
    bool write_ready_;
    std::mutex drain_wait_mutex_;
    std::condition_variable drain_condition_;
    bool drain_ready_;
    stream_callback_t client_callback;
    void *client_cookie;
    static int GetDeviceAddress(struct str_parms *parms, int *card_id,
                                 int *device_num);
    int GetLookupTableIndex(const struct string_to_enum *table,
                                        const int table_size, int value);
protected:
    struct qal_stream_attributes streamAttributes_;
    qal_stream_handle_t*      qal_stream_handle_;
    audio_io_handle_t         handle_;
    qal_device_id_t           qal_device_id_;
    struct audio_config       config_;
    char                      address_[AUDIO_DEVICE_MAX_ADDRESS_LEN];
    bool                      stream_started_ = false;
    bool                      stream_paused_ = false;
    int usecase_;
    struct qal_volume_data *volume_; /* used to cache volume */
    std::map <audio_devices_t, qal_device_id_t> mAndroidDeviceMap;
};

class StreamOutPrimary : public StreamPrimary {

private:
    int mNoOfOutDevices;
    struct qal_device* mQalOutDevice;
    qal_device_id_t* mQalOutDeviceIds;
    audio_devices_t mAndroidOutDevices;
    bool mInitialized;

public:
    StreamOutPrimary(audio_io_handle_t handle,
                     audio_devices_t devices,
                     audio_output_flags_t flags,
                     struct audio_config *config,
                     const char *address,
                     offload_effects_start_output fnp_start_offload_effect,
                     offload_effects_stop_output fnp_stop_offload_effect,
                     visualizer_hal_start_output fnp_visualizer_start_output_,
                     visualizer_hal_stop_output fnp_visualizer_stop_output_);

    ~StreamOutPrimary();
    int Standby();
    int SetVolume(float left, float right);
    uint64_t GetFramesWritten(struct timespec *timestamp);
    int SetParameters(struct str_parms *parms);
    int VoiceSetParameters(std::shared_ptr<AudioDevice> adevice, const char *kvpairs);
    int Pause();
    int Resume();
    int Drain(audio_drain_type_t type);
    int Flush();
    int Start();
    int Stop();
    ssize_t Write(const void *buffer, size_t bytes);
    int Open();
    void GetStreamHandle(audio_stream_out** stream);
    uint32_t GetBufferSize();
    int GetFrames(uint64_t *frames);
    static qal_stream_type_t GetQalStreamType(audio_output_flags_t halStreamFlags);
    static int64_t GetRenderLatency(audio_output_flags_t halStreamFlags);
    int GetOutputUseCase(audio_output_flags_t halStreamFlags);
    int StartOffloadEffects(audio_io_handle_t, qal_stream_handle_t*);
    int StopOffloadEffects(audio_io_handle_t, qal_stream_handle_t*);
    bool CheckOffloadEffectsType(qal_stream_type_t qal_stream_type);
    int StartOffloadVisualizer(audio_io_handle_t, qal_stream_handle_t*);
    int StopOffloadVisualizer(audio_io_handle_t, qal_stream_handle_t*);
    audio_output_flags_t flags_;
    int CreateMmapBuffer(int32_t min_size_frames, struct audio_mmap_buffer_info *info);
    int GetMmapPosition(struct audio_mmap_position *position);
    bool isDeviceAvailable(qal_device_id_t deviceId);
protected:
    struct timespec writeAt;
    int get_compressed_buffer_size();
    int get_pcm_buffer_size();
    audio_format_t halInputFormat = AUDIO_FORMAT_DEFAULT;
    audio_format_t halOutputFormat = AUDIO_FORMAT_DEFAULT;
    uint32_t convertBufSize;
    uint32_t fragments_ = 0;
    uint32_t fragment_size_ = 0;
    qal_snd_dec_t qalSndDec;
    uint32_t msample_rate;
    uint16_t mchannels;
    std::shared_ptr<audio_stream_out>   stream_;
    uint64_t total_bytes_written_; /* total frames written, not cleared when entering standby */
    offload_effects_start_output fnp_offload_effect_start_output_ = nullptr;
    offload_effects_stop_output fnp_offload_effect_stop_output_ = nullptr;
    visualizer_hal_start_output fnp_visualizer_start_output_ = nullptr;
    visualizer_hal_stop_output fnp_visualizer_stop_output_ = nullptr;
    void *convertBuffer;
    int FillHalFnPtrs();
    audio_format_t AlsatoHalFormat(uint32_t pcm_format);
    uint32_t HaltoAlsaFormat(audio_format_t hal_format);
    friend class AudioDevice;
};

class StreamInPrimary : public StreamPrimary{

private:
     int mNoOfInDevices;
     struct qal_device* mQalInDevice;
     qal_device_id_t* mQalInDeviceIds;
     audio_devices_t mAndroidInDevices;
     bool mInitialized;
public:
    StreamInPrimary(audio_io_handle_t handle,
                    audio_devices_t devices,
                    audio_input_flags_t flags,
                    struct audio_config *config,
                    const char *address,
                    audio_source_t source);

    ~StreamInPrimary();
    int Standby();
    int SetGain(float gain);
    void GetStreamHandle(audio_stream_in** stream);
    int Open();
    int Start();
    int Stop();
    ssize_t Read(const void *buffer, size_t bytes);
    uint32_t GetBufferSize();
    qal_stream_type_t GetQalStreamType(audio_input_flags_t halStreamFlags,
            uint32_t sample_rate);
    int GetInputUseCase(audio_input_flags_t halStreamFlags, audio_source_t source);
    int addRemoveAudioEffect(const struct audio_stream *stream, effect_handle_t effect,bool enable);
    int SetParameters(const char *kvpairs);
    bool is_st_session;
    bool is_st_session_active;
    audio_input_flags_t                 flags_;
    int CreateMmapBuffer(int32_t min_size_frames, struct audio_mmap_buffer_info *info);
    int GetMmapPosition(struct audio_mmap_position *position);
    bool isDeviceAvailable(qal_device_id_t deviceId);
protected:
    int FillHalFnPtrs();
    std::shared_ptr<audio_stream_in>    stream_;
    audio_source_t                      source_;
    friend class AudioDevice;
    uint64_t total_bytes_read_; /* total frames written, not cleared when entering standby */
    bool isECEnabled = false;
    bool isNSEnabled = false;
};
#endif  // ANDROID_HARDWARE_AHAL_ASTREAM_H_
