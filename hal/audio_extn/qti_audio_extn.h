/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#ifndef QTI_AUDIO_EXTN_H
#define QTI_AUDIO_EXTN_H

#include <hardware/audio.h>
#include <time.h>

#define QTI_AUDIO_DEVICE_API_VERSION_1_0 HARDWARE_MAKE_API_VERSION_2(1, 0, 0)

typedef struct qti_audio_hw_device qti_audio_hw_device_t;
typedef struct qti_audio_stream_out qti_audio_stream_out_t;
typedef struct qti_audio_stream_in qti_audio_stream_in_t;

/**
 * The "QTI" timestamp
 *
 * This timestamp is the same as the POSIX `timespec' with the following
 * additional requirements:
 *
 * 1. It shall be monotonically increasing.
 * 2. It shall be globally comparable for all audio data.
 * 3. It shall not have any negative values.
 * 4. The epoch shall be no earlier than the last reboot.
 * 5. As a consequence of #4, the timestamp covers at least 68 years.
 *    therefore, the expectation is that we will not need to handle
 *    the overflow case.
 */
typedef struct timespec qti_audio_timestamp_t;

struct qti_audio_hw_device {
    /**
     * Pointer to the standard HAL implementation
     */
    struct audio_hw_device base;

    uint32_t api_version;

    /* Same as audio_hw_device::open_output_stream, but using extn struct */
    int (*qti_open_output_stream)(struct qti_audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  audio_output_flags_t flags,
                                  struct audio_config *config,
                                  struct qti_audio_stream_out **stream,
                                  const char *address);
    /* Same as audio_hw_device::close_output_stream, but using extn struct */
    void (*qti_close_output_stream)(struct qti_audio_hw_device *dev,
                                    struct qti_audio_stream_out *stream);
    /* Same as audio_hw_device::open_input_stream, but using extn struct */
    int (*qti_open_input_stream)(struct qti_audio_hw_device *dev,
                                 audio_io_handle_t handle,
                                 audio_devices_t devices,
                                 struct audio_config *config,
                                 struct qti_audio_stream_in **stream,
                                 audio_input_flags_t flags,
                                 const char *address,
                                 audio_source_t source);
    /* Same as audio_hw_device::close_input_stream, but using extn struct */
    void (*qti_close_input_stream)(struct qti_audio_hw_device *dev,
                                   struct qti_audio_stream_in *stream);
};

struct qti_audio_stream_out
{
    /**
     * Pointer to the standard HAL implementation
     */
    struct audio_stream_out base;

    /**
     * Write audio data to device
     *
     * This is the same as audio_stream_out::write, but with the additional
     * `ts' argument that the device returns.
     *
     * @param ts the device shall return the timestamp for when the first
     * audio sample is expected to play out the speaker. Thus, the timestamp
     * shall be latency-compensated. However, if the stream is in the pause
     * state, it shall return a value of {0,0}.
     */
    ssize_t (*qti_write)(struct qti_audio_stream_out *stream,
                         const void *buffer,
                         size_t bytes,
                         qti_audio_timestamp_t *ts);
};

struct qti_audio_stream_in
{
    /**
     * Pointer to the standard HAL implementation
     */
    struct audio_stream_in base;

    /* buffer pointer to hold timestamp buffer before stripping in-band timestamp */
    void *tbuf;
    size_t tbuf_len;
    /* timestamp meta data, this needs to be preserved in callback
     * as buf manipulations may be performed in hal, and can distort in-place metadata
     */
    void *mdata;
    /**
     * Read audio data from the device
     *
     * This is the same as audio_stream_in::read, but with the additional
     * `ts' argument that the device returns.
     *
     * @param ts the device shall return the timestamp for when the first
     * audio sample actually occured at the microphone. Thus the timestamp
     * shall be latency-compensated.
     */
    ssize_t (*qti_read)(struct qti_audio_stream_in *stream,
                        void *buffer,
                        size_t bytes,
                        qti_audio_timestamp_t *ts);

};

/** convenience API for opening and closing a extended hw device */
static inline int qti_audio_hw_device_open(const struct hw_module_t* module,
                                            struct qti_audio_hw_device** device)
{
    return module->methods->open(module, AUDIO_HARDWARE_INTERFACE,
                                 (struct hw_device_t**)device);
}

static inline int qti_audio_hw_device_close(struct qti_audio_hw_device* device)
{
    return device->base.common.close(&device->base.common);
}
#endif // QTI_AUDIO_EXTN_H
