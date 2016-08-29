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

#ifndef QTI_AUDIO_EXTN_INTERNAL_H
#define QTI_AUDIO_EXTN_INTERNAL_H
#ifdef QTI_AUDIO_EXTN

#include <hardware/audio.h>

/* wrapper to add extended apis to audio hw device instance */
int qti_audio_extn_init(hw_device_t *device);
/* update timestamp immediately after compress read as conversions might distort metadata */
void qti_audio_extn_backup_capture_stream_metadata(struct stream_in *in, void *buffer, size_t bytes_read);
/* compress read is required in case of timestamp mode */
bool qti_audio_extn_is_compressed_input_stream(struct stream_in *in);
/* timestamp mode needs to update fragmet size and timestamp flag in codec config */
void qti_audio_extn_update_config(struct stream_in *in);

#else // QTI_AUDIO_EXTN not defined

#define qti_audio_extn_init(device) (0)
#define qti_audio_extn_backup_capture_stream_metadata(in, buffer, bytes_read) (0)
bool qti_audio_extn_is_compressed_input_stream(struct stream_in *in __unused) {return false;}
#define qti_audio_extn_update_config(in) (0)

#endif // QTI_AUDIO_EXTN
#endif // QTI_AUDIO_EXTN_INTERNAL_H
