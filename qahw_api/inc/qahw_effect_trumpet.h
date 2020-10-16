/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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
 *
 */

#ifndef QAHW_EFFECT_TRUMPET_H_
#define QAHW_EFFECT_TRUMPET_H_

#include <qahw_effect_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QAHW_EFFECT_TRUMPET_LIBRARY "libtrumpet.so"

static const qahw_effect_uuid_t SL_IID_TRUMPET_ = {0x5e46f50b, 0xcc86, 0x4d6e, 0x9610, {0x54, 0x72, 0x76, 0x1d, 0xd0, 0xe2}};
static const qahw_effect_uuid_t * const SL_IID_TRUMPET = &SL_IID_TRUMPET_;

static const qahw_effect_uuid_t SL_IID_TRUMPET_UUID_ = {0xa52a402b, 0xe254, 0x4fb9, 0xab7a, {0x7c, 0xf5, 0x83, 0x44, 0xe3, 0xf3}};
static const qahw_effect_uuid_t * const SL_IID_TRUMPET_UUID = &SL_IID_TRUMPET_UUID_;

/* enumerated parameter settings for trumpet effect used by both set and get*/
typedef enum {
    TRUMPET_PARAM_ENABLE,
    TRUMPET_PARAM_PREGAIN,
    TRUMPET_PARAM_POSTGAIN,
    TRUMPET_PARAM_SYSTEMGAIN,
    TRUMPET_PARAM_MI_DV_LEVELER_STEERING,
    TRUMPET_PARAM_MI_DIALOG_ENHANCER,
    TRUMPET_PARAM_MI_SURROUND_COMPRESSOR,
    TRUMPET_PARAM_MI_IEQ_STEERING,
    TRUMPET_PARAM_DIALOG_AMOUNT,
    TRUMPET_PARAM_DIALOG_DUCKING,
    TRUMPET_PARAM_DIALOG_ENABLE,
    TRUMPET_PARAM_VOLUME_LEVELER_AMOUNT,
    TRUMPET_PARAM_VOLUME_LEVELER_IN_TARGET,
    TRUMPET_PARAM_VOLUME_LEVELER_OUT_TARGET,
    TRUMPET_PARAM_VOLUME_LEVELER_ENABLE,
    TRUMPET_PARAM_VOLUME_MODELER_CALIBRATION,
    TRUMPET_PARAM_VOLUME_MODELER_ENABLE,
    TRUMPET_PARAM_VOLMAX_BOOST,
    TRUMPET_PARAM_BASS_BOOST,
    TRUMPET_PARAM_BASS_CUTOFF_FREQ,
    TRUMPET_PARAM_BASS_WIDTH,
    TRUMPET_PARAM_BASS_ENABLE,
    TRUMPET_PARAM_BASS_EXTRACT_CUTOFF_FREQ,
    TRUMPET_PARAM_BASS_EXTRACT_ENABLE,
    TRUMPET_PARAM_REGULATOR_SET,
    TRUMPET_PARAM_REGULATOR_OVERDRIVE,
    TRUMPET_PARAM_REGULATOR_TIMBRE_PRESERVE,
    TRUMPET_PARAM_REGULATOR_RELAXATION_AMT,
    TRUMPET_PARAM_REGULATOR_SPKR_DIST_ENABLE,
    TRUMPET_PARAM_REGULATOR_ENABLE,
    TRUMPET_PARAM_VIRTUAL_BASS_MODE,
    TRUMPET_PARAM_VIRTUAL_BASS_SRC_FREQ,
    TRUMPET_PARAM_VIRTUAL_BASS_MIX_FREQ,
    TRUMPET_PARAM_VIRTUAL_BASS_OVERALL_GAIN,
    TRUMPET_PARAM_VIRTUAL_BASS_SUBGAINS,
    TRUMPET_PARAM_VIRTUAL_BASS_SLOPE_GAIN,
    TRUMPET_PARAM_FRONT_SPK_ANG,
    TRUMPET_PARAM_SURROUND_SPK_ANG,
    TRUMPET_PARAM_HEIGHT_SPK_ANG,
    TRUMPET_PARAM_HEIGHT_FILTER_MODE,
    TRUMPET_PARAM_SURROUND_BOOST,
    TRUMPET_PARAM_SURROUND_DECODER_ENABLE,
    TRUMPET_PARAM_CALIBRATION,
    TRUMPET_PARAM_GRAPHICS_ENABLE,
    TRUMPET_PARAM_GRAPHICS_SET,
    TRUMPET_PARAM_AUDIO_OPTIMIZER_ENABLE,
    TRUMPET_PARAM_AUDIO_OPTIMIZER_SET,
    TRUMPET_PARAM_PROCESS_OPTIMIZER_ENABLE,
    TRUMPET_PARAM_PROCESS_OPTIMIZER_SET,
    TRUMPET_PARAM_IEQ_ENABLE,
    TRUMPET_PARAM_IEQ_AMOUNT,
    TRUMPET_PARAM_IEQ_SET,
    TRUMPET_PARAM_OP_MODE,
    TRUMPET_PARAM_DYNAMIC_PARAMETER,
    TRUMPET_PARAM_INIT_INFO,
    TRUMPET_PARAM_CENTER_SPREADING,
    TRUMPET_PARAM_METADATA_PARAM,
    TRUMPET_PARAM_CIDK_VALIDATE = 100,
} qahw_trumpet_params_t;

#ifdef __cplusplus
}  // extern "C"
#endif


#endif /*QAHW_EFFECT_TRUMPET_H_*/
