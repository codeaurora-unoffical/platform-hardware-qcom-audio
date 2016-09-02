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

#define LOG_TAG "qti_audio_extn"
/* #define LOG_NDEBUG 0 */
#include <hardware/audio.h>
#include <errno.h>
#include <time.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "qti_audio_extn.h"
#include "qti_audio_extn_internal.h"
#include "sound/compress_params.h"

/* default timestamp metadata definition if not defined in kernel*/
#ifndef COMPRESSED_TIMESTAMP_FLAG
#define COMPRESSED_TIMESTAMP_FLAG 0
struct snd_codec_metadata {
long timestamp;
};
#define compress_config_set_timstamp_flag(config) (-ENOSYS)
#else
#define compress_config_set_timstamp_flag(config) \
            (config)->codec->flags |= COMPRESSED_TIMESTAMP_FLAG
#endif

static ssize_t qti_out_write(struct qti_audio_stream_out *stream,
                             const void *buffer,
                             size_t bytes,
                             qti_audio_timestamp_t *ts __unused)
{
    return stream->base.write(&stream->base, buffer, bytes);
}

static ssize_t qti_in_read(struct qti_audio_stream_in *stream,
                           void *buffer,
                           size_t bytes,
                           qti_audio_timestamp_t *ts)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct snd_codec_metadata *mdata = NULL;
    size_t offset = sizeof(struct snd_codec_metadata);
    char *buf = NULL;
    size_t ret = 0;

    if (ts) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
    }

    if (!(in->flags & AUDIO_INPUT_FLAG_TIMESTAMP))
        return stream->base.read(&stream->base, buffer, bytes);

    ALOGV("stream %p, tbuf %p, tbuf_len %zd, user_buf %p, bytes %zd, bytes_total %zd",
          stream, stream->tbuf, stream->tbuf_len, buffer, bytes, bytes + offset);

    if (stream->tbuf_len < bytes + offset) {
        void *tbuf = calloc(1, bytes + offset);
        if (!tbuf) {
            ALOGE("%s:fatal:cannot allocate memory for read!", __func__);
            return -ENOMEM;
        }
        free(stream->tbuf);
        stream->tbuf = tbuf;
        stream->tbuf_len = bytes + offset;
        ALOGD("updated tbuf, new ptr %p, len %zd", stream->tbuf, stream->tbuf_len);
    }

    ret = stream->base.read(&stream->base, stream->tbuf, bytes + offset);
    if (ret == (bytes + offset))  {
        buf = (char *)stream->tbuf + offset;
        memcpy(buffer, (void *)buf, bytes);
        if (ts) {
            /* Metadata for fromat 8_24 is cached from callback
             * to avoid conversions performed in hal.
             * Other formats can get metadata directly from read buffer.
             */
            void *ptr_mdata = (in->format == AUDIO_FORMAT_PCM_8_24_BIT) ?
                              stream->mdata : stream->tbuf;
            mdata = (struct snd_codec_metadata *) ptr_mdata;
            ts->tv_sec = mdata ? mdata->timestamp : 0;
        }
    } else {
        ALOGE("error read returned %zd", ret);
        return ret;
    }

    return bytes;
}

static int qti_audio_open_output_stream(struct qti_audio_hw_device *dev,
                                         audio_io_handle_t handle,
                                         audio_devices_t devices,
                                         audio_output_flags_t flags,
                                         struct audio_config *config,
                                         struct qti_audio_stream_out **stream,
                                         const char *address)
{
    struct audio_stream_out *aso = NULL;
    int ret = 0;

    ret = dev->base.open_output_stream(&dev->base, handle, devices,
                                       flags, config, &aso, address);
    if (ret)
        return ret;

    *stream = (struct qti_audio_stream_out *)(aso);
    (*stream)->qti_write = qti_out_write;
    return ret;
}

static void qti_audio_close_output_stream(struct qti_audio_hw_device *dev,
                                    struct qti_audio_stream_out *stream)
{
    dev->base.close_output_stream(&dev->base, &stream->base);
}

static int qti_audio_open_input_stream(struct qti_audio_hw_device *dev,
                                        audio_io_handle_t handle,
                                        audio_devices_t devices,
                                        struct audio_config *config,
                                        struct qti_audio_stream_in **stream,
                                        audio_input_flags_t flags,
                                        const char *address,
                                        audio_source_t source)
{
    struct audio_stream_in *asi = NULL;
    size_t size = 0;
    int ret = 0;

    ret = dev->base.open_input_stream(&dev->base, handle, devices,
                                      config, &asi, flags, address, source);
    if (ret)
        return ret;

    *stream = (struct qti_audio_stream_in *)asi;
    if (COMPRESSED_TIMESTAMP_FLAG &&
        (flags & AUDIO_INPUT_FLAG_TIMESTAMP)) {
        size_t mdata_size = sizeof(struct snd_codec_metadata);
        size = mdata_size + asi->common.get_buffer_size(&asi->common);
        void *buf = calloc(1, size);
        if (buf != NULL) {
            (*stream)->tbuf = buf;
            (*stream)->tbuf_len = size;
        }
        (*stream)->mdata = calloc(1, mdata_size);
    } else {
        (*stream)->tbuf = NULL;
        (*stream)->tbuf_len = 0;
        (*stream)->mdata = NULL;
    }

    ALOGD("stream %p, tbuf %p, tbuf_len %zd, mdata %p",
          asi, (*stream)->tbuf, size, (*stream)->mdata);

    (*stream)->qti_read = qti_in_read;
    return ret;
}

static void qti_audio_close_input_stream(struct qti_audio_hw_device *dev,
                                          struct qti_audio_stream_in *stream)
{
    dev->base.close_input_stream(&dev->base, &stream->base);

    if (stream->tbuf) {
        free(stream->tbuf);
        stream->tbuf = NULL;
        stream->tbuf_len = 0;
    }
    if (stream->mdata)
        free(stream->mdata);
}

/* copy in band timestamp metdata if present */
void qti_audio_extn_backup_capture_stream_metadata(struct stream_in *in,
                                           void *buffer, size_t bytes_read)
{
    struct qti_audio_stream_in *stream = (struct qti_audio_stream_in *) in;
    size_t mdata_size = sizeof(struct snd_codec_metadata);

    ALOGV("%s: in %p, buf %p, btes %zd", __func__, in, buffer, bytes_read);
    if ((in->flags & AUDIO_INPUT_FLAG_TIMESTAMP) && bytes_read > mdata_size) {
        if (!stream->mdata)
            stream->mdata = malloc(mdata_size);
        if (!stream->mdata) {
            ALOGE("%s: fatal: cannot alloc memory for metadata!", __func__);
            return;
        }
        memcpy(stream->mdata, buffer, mdata_size);
    }
}

bool qti_audio_extn_is_compressed_input_stream(struct stream_in *in)
{
    if (COMPRESSED_TIMESTAMP_FLAG && (in->flags & AUDIO_INPUT_FLAG_TIMESTAMP))
        return true;

    return false;
}

void qti_audio_extn_update_config(struct stream_in *in)
{
    size_t mdata_size = sizeof(struct snd_codec_metadata);

    if (COMPRESSED_TIMESTAMP_FLAG && (in->flags & AUDIO_INPUT_FLAG_TIMESTAMP)) {
        in->compr_config.fragment_size += mdata_size;
        compress_config_set_timstamp_flag(&in->compr_config);
    }
}

int qti_audio_extn_init(hw_device_t *device)
{
    struct qti_audio_hw_device *qti_device = NULL;
    ALOGD("%s", __func__);
    if (!device)
        return -EINVAL;
    qti_device = (struct qti_audio_hw_device *)device;
    qti_device->api_version = QTI_AUDIO_DEVICE_API_VERSION_1_0;
    qti_device->qti_open_output_stream = qti_audio_open_output_stream;
    qti_device->qti_close_output_stream = qti_audio_close_output_stream;
    qti_device->qti_open_input_stream = qti_audio_open_input_stream;
    qti_device->qti_close_input_stream = qti_audio_close_input_stream;

    return 0;
}
