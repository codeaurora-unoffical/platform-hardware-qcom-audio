/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_anc_ext"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <audio_hw.h>
#include "audio_extn.h"
#include "platform_api.h"
#include "platform.h"
#include "audio-anc-ext.h"
#include "audio_hal_plugin.h"

#ifdef VHAL_HELPER_ENABLED
#include <vehicle-hal-audio-helper-for-c.h>
#endif

#ifdef EXT_AUDIO_ANC_ENABLED

#ifdef VHAL_HELPER_ENABLED

static vehicle_hal_audio_helper_t    *vhal_audio_helper = NULL;

int32_t anc_extn_ext_hw_plugin_codec_enable(vehicle_hal_audio_helper_t  *vhal_audio_helper, audio_hal_plugin_codec_enable_t *codec_enable)
{
    char *str = NULL;
    int ret = 0;
    struct str_parms *parms = str_parms_create();

    if (vhal_audio_helper == NULL || codec_enable == NULL) {
        ALOGE("%s: received null pointer", __func__);
        ret = -EINVAL;
        goto end;
    }

    if (parms == NULL) {
        ALOGE("%s:parms is NULL",__func__);
        ret = -ENOMEM;
        goto end;
    }

    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE, AUDIO_HAL_PLUGIN_MSG_CODEC_ENABLE);
    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC, codec_enable->usecase);
    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_SND_DEVICE, codec_enable->snd_dev);
    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK, codec_enable->num_chs);

    str = str_parms_to_str(parms);
    if (str == NULL) {
        ALOGE("%s: str is NULL", __func__);
        ret = -ENOMEM;
        goto end;
    }

    ALOGD("%s: params = %s", __func__, str);
    // notify audio params.
    ret = vehicle_hal_audio_helper_set_parameters(vhal_audio_helper, (const char*)str);
    if (ret < 0) {
        ALOGE("%s: set parameters failed", __func__);
        ret = -EINVAL;
        goto end;
    }

end:
    if (parms != NULL)
        str_parms_destroy(parms);
    if (str != NULL)
        free(str);
    return ret;
}

int32_t anc_extn_ext_hw_plugin_codec_disable(vehicle_hal_audio_helper_t  *vhal_audio_helper, audio_hal_plugin_codec_disable_t *codec_disable)
{
    char *str = NULL;
    int ret = 0;
    struct str_parms *parms = str_parms_create();

    if (vhal_audio_helper == NULL || codec_disable == NULL) {
        ALOGE("%s: received null pointer", __func__);
        ret = -EINVAL;
        goto end;
    }

    if (parms == NULL) {
        ALOGE("%s:parms is NULL",__func__);
        ret = -ENOMEM;
        goto end;
    }

    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE, AUDIO_HAL_PLUGIN_MSG_CODEC_DISABLE);
    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC, codec_disable->usecase);
    str_parms_add_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_SND_DEVICE, codec_disable->snd_dev);

    str = str_parms_to_str(parms);
    if (str == NULL) {
        ALOGE("%s: str is NULL", __func__);
        ret = -ENOMEM;
        goto end;
    }

    ALOGD("%s: params = %s", __func__, str);
    // notify audio params.
    ret = vehicle_hal_audio_helper_set_parameters(vhal_audio_helper, (const char*)str);
    if (ret < 0) {
        ALOGE("%s: set parameters failed", __func__);
        ret = -EINVAL;
        goto end;
    }

end:
    if (parms != NULL)
        str_parms_destroy(parms);
    if (str != NULL)
        free(str);
    return ret;
}
#endif  //#ifdef VHAL_HELPER_ENABLED


static int anc_enable_codec(void)
{
   int ret = 0;

#ifdef VHAL_HELPER_ENABLED
    if (vhal_audio_helper) {
      audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_ENABLE;
      audio_hal_plugin_codec_enable_t codec_enable;

      //enable SPKR path
      codec_enable.snd_dev = SND_DEVICE_OUT_BUS;
      codec_enable.sample_rate = 48000;
      codec_enable.bit_width = 16;   //24;   //16
      codec_enable.num_chs = 2;
      codec_enable.usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_PLAYBACK;

      ret = anc_extn_ext_hw_plugin_codec_enable(vhal_audio_helper, &codec_enable);
      if(ret) {
          ALOGE("%s: enable audio hal plugin SPKR path failed ret = %d",
              __func__, ret);
          goto done;
      }

      //enable MIC path
      codec_enable.snd_dev = SND_DEVICE_IN_BUS;
      codec_enable.sample_rate = 48000;
      codec_enable.bit_width = 24;   //16
      codec_enable.num_chs = 2;
      codec_enable.usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_CAPTURE;

      ret = anc_extn_ext_hw_plugin_codec_enable(vhal_audio_helper, &codec_enable);
      if(ret) {
          ALOGE("%s: enable audio hal plugin MIC path failed ret = %d",
              __func__, ret);
          goto done;
      }
   }

done:
#endif //#ifdef VHAL_HELPER_ENABLED

   return ret;
}

static int anc_disable_codec(void)
{
   int ret = 0;

#ifdef VHAL_HELPER_ENABLED
    if (vhal_audio_helper) {
      audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_DISABLE;
      audio_hal_plugin_codec_disable_t codec_disable;

      //disable MIC path
      codec_disable.snd_dev = 0;
      codec_disable.usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_CAPTURE;

      ret = anc_extn_ext_hw_plugin_codec_disable(vhal_audio_helper, &codec_disable);
      if(ret) {
          ALOGE("%s: disable audio hal plugin MIC path failed ret = %d",
              __func__, ret);
      }

     //disable SPKR path
      codec_disable.snd_dev = 0;
      codec_disable.usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_PLAYBACK;

      ret = anc_extn_ext_hw_plugin_codec_disable(vhal_audio_helper, &codec_disable);
      if(ret) {
          ALOGE("%s: disable audio hal plugin SPKR path failed ret = %d",
              __func__, ret);
      }
   }
#endif //#ifndef VHAL_HELPER_ENABLED

   return ret;
}


#define LIB_AUDIO_ANC "libaudioanc.so"

/* Audio ANC extension lib related functions */
typedef int (*audio_anc_ext_init_t)();
typedef int (*audio_anc_ext_deinit_t)();
typedef int (*audio_anc_ext_send_param_t)(int, void*);
typedef int (*audio_anc_ext_get_param_t)(int, void*);


struct audio_anc_ext_data {
    struct audio_device           *adev;
    void  *anc_ext_lib_handle;
    audio_anc_ext_init_t audio_anc_ext_init;
    audio_anc_ext_deinit_t audio_anc_ext_deinit;
    audio_anc_ext_send_param_t audio_anc_ext_send_param;
    audio_anc_ext_get_param_t audio_anc_ext_get_param;
    bool  run_state;
};

static int32_t ext_anc_start(struct audio_anc_ext_data *my_handle)
{
     int32_t ret = 0;

     if ((my_handle) && (my_handle->audio_anc_ext_send_param)) {

        if(my_handle->run_state) {
           //start already, do nothing
           ALOGD("%s: EXT_ANC start already run_state %d\n", __func__, (int)my_handle->run_state);
        }
        else {
             //enable codec
            ret = anc_enable_codec();

            if (ret) {
                  ALOGE("%s: ANC codec enable failed ret %d\n", __func__, ret);
                  return ret;
            }

            ret = my_handle->audio_anc_ext_send_param(ANC_EXT_CMD_START, NULL);
            if (ret) {
                  ALOGE("%s: ANC_EXT_CMD_START failed ret %d\n", __func__, ret);
                  return ret;
            }

           my_handle->run_state = true;
        }
     }

    return ret;
}

static int32_t ext_anc_stop(struct audio_anc_ext_data *my_handle)
{
    int32_t ret = 0;

    if ((my_handle) && (my_handle->audio_anc_ext_send_param)) {

       if(!my_handle->run_state) {
          //stop already, do nothing
           ALOGD("%s: EXT_ANC stop already run_state %d\n", __func__, (int)my_handle->run_state);
       } else {
          //disable codec
           ret = anc_disable_codec();
           if (ret) {
               ALOGE("%s: ANC disbale codec failed ret %d\n", __func__, ret);
               return ret;
           }
           ret = my_handle->audio_anc_ext_send_param(ANC_EXT_CMD_STOP, NULL);
           if (ret) {
               ALOGE("%s: ANC_EXT_CMD_STOP failed ret %d\n", __func__, ret);
               return ret;
           }
           my_handle->run_state = false;
       }
    }
    return ret;
}


void* audio_extn_ext_audio_anc_init(struct audio_device *adev)
{
    int32_t ret = 0;
    struct audio_anc_ext_data *my_handle = NULL;

    my_handle = calloc(1, sizeof(struct audio_anc_ext_data));

    if (my_handle == NULL) {
        ALOGE("[%s] Memory allocation failed for audio_anc_ext_data",__func__);
        return NULL;
    }

    my_handle->adev = adev;

    my_handle->anc_ext_lib_handle = dlopen(LIB_AUDIO_ANC, RTLD_NOW);

    if(my_handle->anc_ext_lib_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_AUDIO_ANC);
          goto audio_anc_init_fail;
    }
    else
    {
        my_handle->audio_anc_ext_init = (audio_anc_ext_init_t)dlsym(my_handle->anc_ext_lib_handle,
                                                       "audio_anc_ext_init");
        if (!my_handle->audio_anc_ext_init)
            ALOGE("%s: Could not find the symbol audio_anc_ext_init from %s",
                  __func__, LIB_AUDIO_ANC);

        my_handle->audio_anc_ext_deinit = (audio_anc_ext_deinit_t)dlsym(my_handle->anc_ext_lib_handle,
                                                       "audio_anc_ext_deinit");
        if (!my_handle->audio_anc_ext_deinit)
            ALOGE("%s: Could not find the symbol audio_anc_ext_deinit from %s",
                  __func__, LIB_AUDIO_ANC);

        my_handle->audio_anc_ext_send_param = (audio_anc_ext_send_param_t)dlsym(my_handle->anc_ext_lib_handle,
                                                       "audio_anc_ext_send_param");
        if (!my_handle->audio_anc_ext_send_param)
            ALOGE("%s: Could not find the symbol audio_anc_ext_send_param from %s",
                  __func__, LIB_AUDIO_ANC);

        my_handle->audio_anc_ext_get_param = (audio_anc_ext_get_param_t)dlsym(my_handle->anc_ext_lib_handle,
                                                       "audio_anc_ext_get_param");
        if (!my_handle->audio_anc_ext_get_param)
            ALOGE("%s: Could not find the symbol audio_anc_ext_get_param from %s",
                  __func__, LIB_AUDIO_ANC);

        if (my_handle->audio_anc_ext_init)
        {
           ret = my_handle->audio_anc_ext_init();
           ALOGE("audio_anc_ext_init ret %d\n", ret);
        }
    }

#ifdef VHAL_HELPER_ENABLED
    vhal_audio_helper = vehicle_hal_audio_helper_create_with_default_timeout();
    if (vhal_audio_helper == NULL) {
        ALOGE("%s: vhal audio helper not allocated", __func__);
        goto audio_anc_init_fail;
    }
#endif //#ifndef VHAL_HELPER_ENABLED

    return my_handle;

audio_anc_init_fail:
    if(my_handle->anc_ext_lib_handle != NULL)
        dlclose(my_handle->anc_ext_lib_handle);
    free(my_handle);

    return NULL;
}

int32_t audio_extn_ext_audio_anc_deinit(void *handle)
{
    int32_t ret = 0;
    struct audio_anc_ext_data *my_handle = (struct audio_anc_ext_data *)handle;

    if (my_handle == NULL) {
        ALOGE("[%s] NULL audio_anc_ext handle",__func__);
        return -EINVAL;
    }

    if (my_handle->audio_anc_ext_deinit) {
        ret = my_handle->audio_anc_ext_deinit();
        if (ret) {
            ALOGE("%s: audio_anc_ext_deinit failed with ret = %d",
                  __func__, ret);
        }
    }

    if(my_handle->anc_ext_lib_handle != NULL)
        dlclose(my_handle->anc_ext_lib_handle);
    free(my_handle);

#ifdef VHAL_HELPER_ENABLED
    if (vhal_audio_helper != NULL)
        vehicle_hal_audio_helper_destroy(vhal_audio_helper);
#endif //#ifndef VHAL_HELPER_ENABLED

    return ret;
}

int audio_extn_ext_audio_anc_set_parameters(void *handle,
                                           struct str_parms *parms)
{
    int32_t val;
    int32_t ret = 0, err;
    struct audio_anc_ext_data *my_handle = NULL;

    if (handle == NULL || parms == NULL) {
        ALOGE("[%s] received null pointer",__func__);
        return -EINVAL;
    }

    my_handle = (struct audio_anc_ext_data *)handle;
    if (!my_handle->audio_anc_ext_send_param) {
        ALOGE("%s: NULL audio_anc_ext_send_param func ptr", __func__);
        return -EINVAL;
    }

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_CMD_TYPE, &val);
    if (err < 0) {
        ALOGE("%s: Invalid or missing cmd param for audio_anc_ext", __func__);
        return -EINVAL;
    }
    ALOGD("%s: received audio_anc_ext cmd (%d)", __func__, val);
    str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_CMD_TYPE);

    switch(val) {
       case ANC_EXT_CMD_START:
       {
          ret = ext_anc_start(my_handle);
          break;
       }
       case ANC_EXT_CMD_STOP:
       {
          ret = ext_anc_stop(my_handle);
          break;
       }
       case ANC_EXT_CMD_BYPASS_MODE:
       {
          int32_t bypass_mode = 0;
          struct audio_anc_bypass_mode_ext my_bypass_mode;

          err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_BYPASS_MODE,
                &bypass_mode);
          if (err < 0) {
               ALOGE("%s: Invalid or missing usecase param for audio-ext-anc", __func__);
               ret = -EINVAL;
               goto done;
          }
          str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_BYPASS_MODE);
          ALOGD("%s: received audio_anc_ext bypass_mode (%d)", __func__, bypass_mode);

          my_bypass_mode.mode = bypass_mode;
          ret = my_handle->audio_anc_ext_send_param(ANC_EXT_CMD_BYPASS_MODE, (void*)&my_bypass_mode);
          if (ret) {
              ALOGE("%s: ANC_EXT_CMD_BYPASS_MODE failed ret %d\n", __func__, ret);
              return ret;
          }
          break;
       }
       case ANC_EXT_CMD_RPM:
       {
          int32_t rpm = 0;
          struct audio_anc_rpm_info_ext my_rpm;

          err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_RPM,
                &rpm);
          if (err < 0) {
              ALOGE("%s: Invalid or missing usecase param for audio-ext-anc", __func__);
              ret = -EINVAL;
              goto done;
          }
          str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_RPM);
          ALOGD("%s: received audio_anc_ext rpm (%d)", __func__, rpm);

          my_rpm.rpm = rpm;

          ret = my_handle->audio_anc_ext_send_param(ANC_EXT_CMD_RPM, (void*)&my_rpm);
          if (ret) {
              ALOGE("%s: ANC_EXT_CMD_RPM failed ret %d\n", __func__, ret);
              return ret;
          }
          break;
       }
       case ANC_EXT_CMD_ALGO_MODULE:
       {
          int32_t algo_module_id = 0;
          struct audio_anc_algo_module_info_ext my_module_info;

          err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_ALGO_MODULE_ID,
                &algo_module_id);
          if (err < 0) {
              ALOGE("%s: Invalid or missing usecase param for audio-ext-anc", __func__);
              ret = -EINVAL;
              goto done;
          }
          str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_ALGO_MODULE_ID);
          ALOGD("%s: received audio_anc_ext algo_module_id (%d)", __func__, algo_module_id);

          my_module_info.module_id = algo_module_id;

          ret = my_handle->audio_anc_ext_send_param(ANC_EXT_CMD_ALGO_MODULE, (void*)&my_module_info);
          if (ret) {
              ALOGE("%s: ANC_EXT_CMD_ALGO_MODULE failed ret %d\n", __func__, ret);
              return ret;
          }
          break;
       }
       default:
          ALOGE("%s: Invalid plugin message type: %d", __func__, val);
               ret = -EINVAL;
    }

done:
    ALOGI("%s: exit with code(%d)", __func__, ret);
    return ret;
}

#endif /* EXT_AUDIO_ANC_ENABLED */
