/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Konstantinos Natsakis <konstantinos.natsakis@gmail.com>
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "alMain.h"
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include <pulse/pulseaudio.h>

#if PA_API_VERSION == 11
#define PA_STREAM_ADJUST_LATENCY 0x2000U
static __inline int PA_STREAM_IS_GOOD(pa_stream_state_t x)
{
    return (x == PA_STREAM_CREATING || x == PA_STREAM_READY);
}
static __inline int PA_CONTEXT_IS_GOOD(pa_context_state_t x)
{
    return (x == PA_CONTEXT_CONNECTING || x == PA_CONTEXT_AUTHORIZING ||
            x == PA_CONTEXT_SETTING_NAME || x == PA_CONTEXT_READY);
}
#define PA_STREAM_IS_GOOD PA_STREAM_IS_GOOD
#define PA_CONTEXT_IS_GOOD PA_CONTEXT_IS_GOOD
#elif PA_API_VERSION != 12
#error Invalid PulseAudio API version
#endif

#ifndef PA_CHECK_VERSION
#define PA_CHECK_VERSION(major,minor,micro)                             \
    ((PA_MAJOR > (major)) ||                                            \
     (PA_MAJOR == (major) && PA_MINOR > (minor)) ||                     \
     (PA_MAJOR == (major) && PA_MINOR == (minor) && PA_MICRO >= (micro)))
#endif

static void *pa_handle;
#define MAKE_FUNC(x) static typeof(x) * p##x
MAKE_FUNC(pa_context_unref);
MAKE_FUNC(pa_sample_spec_valid);
MAKE_FUNC(pa_stream_drop);
MAKE_FUNC(pa_strerror);
MAKE_FUNC(pa_context_get_state);
MAKE_FUNC(pa_stream_get_state);
MAKE_FUNC(pa_threaded_mainloop_signal);
MAKE_FUNC(pa_stream_peek);
MAKE_FUNC(pa_threaded_mainloop_wait);
MAKE_FUNC(pa_threaded_mainloop_unlock);
MAKE_FUNC(pa_threaded_mainloop_in_thread);
MAKE_FUNC(pa_context_new);
MAKE_FUNC(pa_threaded_mainloop_stop);
MAKE_FUNC(pa_context_disconnect);
MAKE_FUNC(pa_threaded_mainloop_start);
MAKE_FUNC(pa_threaded_mainloop_get_api);
MAKE_FUNC(pa_context_set_state_callback);
MAKE_FUNC(pa_stream_write);
MAKE_FUNC(pa_xfree);
MAKE_FUNC(pa_stream_connect_record);
MAKE_FUNC(pa_stream_connect_playback);
MAKE_FUNC(pa_stream_readable_size);
MAKE_FUNC(pa_stream_cork);
MAKE_FUNC(pa_stream_is_suspended);
MAKE_FUNC(pa_stream_get_device_name);
MAKE_FUNC(pa_path_get_filename);
MAKE_FUNC(pa_get_binary_name);
MAKE_FUNC(pa_threaded_mainloop_free);
MAKE_FUNC(pa_context_errno);
MAKE_FUNC(pa_xmalloc);
MAKE_FUNC(pa_stream_unref);
MAKE_FUNC(pa_threaded_mainloop_accept);
MAKE_FUNC(pa_stream_set_write_callback);
MAKE_FUNC(pa_threaded_mainloop_new);
MAKE_FUNC(pa_context_connect);
MAKE_FUNC(pa_stream_set_buffer_attr);
MAKE_FUNC(pa_stream_get_buffer_attr);
MAKE_FUNC(pa_stream_get_sample_spec);
MAKE_FUNC(pa_stream_set_read_callback);
MAKE_FUNC(pa_stream_set_state_callback);
MAKE_FUNC(pa_stream_set_moved_callback);
MAKE_FUNC(pa_stream_new);
MAKE_FUNC(pa_stream_disconnect);
MAKE_FUNC(pa_threaded_mainloop_lock);
MAKE_FUNC(pa_channel_map_init_auto);
MAKE_FUNC(pa_channel_map_parse);
MAKE_FUNC(pa_channel_map_snprint);
MAKE_FUNC(pa_channel_map_equal);
MAKE_FUNC(pa_context_get_server_info);
MAKE_FUNC(pa_context_get_sink_info_by_name);
MAKE_FUNC(pa_context_get_sink_info_list);
MAKE_FUNC(pa_context_get_source_info_list);
MAKE_FUNC(pa_operation_get_state);
MAKE_FUNC(pa_operation_unref);
#if PA_CHECK_VERSION(0,9,15)
MAKE_FUNC(pa_channel_map_superset);
MAKE_FUNC(pa_stream_set_buffer_attr_callback);
#endif
#if PA_CHECK_VERSION(0,9,16)
MAKE_FUNC(pa_stream_begin_write);
#endif
#undef MAKE_FUNC

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char *device_name;

    ALCuint samples;
    ALCuint frame_size;

    RingBuffer *ring;

    pa_buffer_attr attr;
    pa_sample_spec spec;

    pa_threaded_mainloop *loop;

    pa_stream *stream;
    pa_context *context;
} pulse_data;

typedef struct {
    char *name;
    char *device_name;
} DevMap;


static const ALCchar pulse_device[] = "PulseAudio Software";
static DevMap *allDevNameMap;
static ALuint numDevNames;
static DevMap *allCaptureDevNameMap;
static ALuint numCaptureDevNames;
static pa_context_flags_t pulse_ctx_flags;


void *pulse_load(void) //{{{
{
    if(!pa_handle)
    {
#ifdef _WIN32
        pa_handle = LoadLibrary("libpulse-0.dll");
#define LOAD_FUNC(x) do { \
    p##x = (typeof(p##x))GetProcAddress(pa_handle, #x); \
    if(!(p##x)) { \
        AL_PRINT("Could not load %s from libpulse-0.dll\n", #x); \
        FreeLibrary(pa_handle); \
        pa_handle = NULL; \
        return NULL; \
    } \
} while(0)
#define LOAD_OPTIONAL_FUNC(x) do { \
    p##x = (typeof(p##x))GetProcAddress(pa_handle, #x); \
} while(0)

#elif defined (HAVE_DLFCN_H)

        const char *err;
#if defined(__APPLE__) && defined(__MACH__)
        pa_handle = dlopen("libpulse.0.dylib", RTLD_NOW);
#else
        pa_handle = dlopen("libpulse.so.0", RTLD_NOW);
#endif
        dlerror();

#define LOAD_FUNC(x) do { \
    p##x = dlsym(pa_handle, #x); \
    if((err=dlerror()) != NULL) { \
        AL_PRINT("Could not load %s from libpulse: %s\n", #x, err); \
        dlclose(pa_handle); \
        pa_handle = NULL; \
        return NULL; \
    } \
} while(0)
#define LOAD_OPTIONAL_FUNC(x) do { \
    p##x = dlsym(pa_handle, #x); \
    if((err=dlerror()) != NULL) { \
        p##x = NULL; \
    } \
} while(0)

#else

        pa_handle = (void*)0xDEADBEEF;
#define LOAD_FUNC(x) p##x = (x)
#define LOAD_OPTIONAL_FUNC(x) p##x = (x)

#endif
        if(!pa_handle)
            return NULL;

LOAD_FUNC(pa_context_unref);
LOAD_FUNC(pa_sample_spec_valid);
LOAD_FUNC(pa_stream_drop);
LOAD_FUNC(pa_strerror);
LOAD_FUNC(pa_context_get_state);
LOAD_FUNC(pa_stream_get_state);
LOAD_FUNC(pa_threaded_mainloop_signal);
LOAD_FUNC(pa_stream_peek);
LOAD_FUNC(pa_threaded_mainloop_wait);
LOAD_FUNC(pa_threaded_mainloop_unlock);
LOAD_FUNC(pa_threaded_mainloop_in_thread);
LOAD_FUNC(pa_context_new);
LOAD_FUNC(pa_threaded_mainloop_stop);
LOAD_FUNC(pa_context_disconnect);
LOAD_FUNC(pa_threaded_mainloop_start);
LOAD_FUNC(pa_threaded_mainloop_get_api);
LOAD_FUNC(pa_context_set_state_callback);
LOAD_FUNC(pa_stream_write);
LOAD_FUNC(pa_xfree);
LOAD_FUNC(pa_stream_connect_record);
LOAD_FUNC(pa_stream_connect_playback);
LOAD_FUNC(pa_stream_readable_size);
LOAD_FUNC(pa_stream_cork);
LOAD_FUNC(pa_stream_is_suspended);
LOAD_FUNC(pa_stream_get_device_name);
LOAD_FUNC(pa_path_get_filename);
LOAD_FUNC(pa_get_binary_name);
LOAD_FUNC(pa_threaded_mainloop_free);
LOAD_FUNC(pa_context_errno);
LOAD_FUNC(pa_xmalloc);
LOAD_FUNC(pa_stream_unref);
LOAD_FUNC(pa_threaded_mainloop_accept);
LOAD_FUNC(pa_stream_set_write_callback);
LOAD_FUNC(pa_threaded_mainloop_new);
LOAD_FUNC(pa_context_connect);
LOAD_FUNC(pa_stream_set_buffer_attr);
LOAD_FUNC(pa_stream_get_buffer_attr);
LOAD_FUNC(pa_stream_get_sample_spec);
LOAD_FUNC(pa_stream_set_read_callback);
LOAD_FUNC(pa_stream_set_state_callback);
LOAD_FUNC(pa_stream_set_moved_callback);
LOAD_FUNC(pa_stream_new);
LOAD_FUNC(pa_stream_disconnect);
LOAD_FUNC(pa_threaded_mainloop_lock);
LOAD_FUNC(pa_channel_map_init_auto);
LOAD_FUNC(pa_channel_map_parse);
LOAD_FUNC(pa_channel_map_snprint);
LOAD_FUNC(pa_channel_map_equal);
LOAD_FUNC(pa_context_get_server_info);
LOAD_FUNC(pa_context_get_sink_info_by_name);
LOAD_FUNC(pa_context_get_sink_info_list);
LOAD_FUNC(pa_context_get_source_info_list);
LOAD_FUNC(pa_operation_get_state);
LOAD_FUNC(pa_operation_unref);
#if PA_CHECK_VERSION(0,9,15)
LOAD_OPTIONAL_FUNC(pa_channel_map_superset);
LOAD_OPTIONAL_FUNC(pa_stream_set_buffer_attr_callback);
#endif
#if PA_CHECK_VERSION(0,9,16)
LOAD_OPTIONAL_FUNC(pa_stream_begin_write);
#endif

#undef LOAD_OPTIONAL_FUNC
#undef LOAD_FUNC
    }
    return pa_handle;
} //}}}

// PulseAudio Event Callbacks //{{{
static void context_state_callback(pa_context *context, void *pdata) //{{{
{
    pa_threaded_mainloop *loop = pdata;
    pa_context_state_t state;

    state = ppa_context_get_state(context);
    if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
        ppa_threaded_mainloop_signal(loop, 0);
}//}}}

static void stream_state_callback(pa_stream *stream, void *pdata) //{{{
{
    pa_threaded_mainloop *loop = pdata;
    pa_stream_state_t state;

    state = ppa_stream_get_state(stream);
    if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
        ppa_threaded_mainloop_signal(loop, 0);
}//}}}

static void stream_buffer_attr_callback(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    SuspendContext(NULL);

    data->attr = *(ppa_stream_get_buffer_attr(stream));
    Device->UpdateSize = 20 * Device->Frequency / 1000;
    Device->NumUpdates = data->attr.tlength/data->frame_size / Device->UpdateSize;
    if(Device->NumUpdates == 0)
        Device->NumUpdates = 1;

    ProcessContext(NULL);
}//}}}

static void stream_device_callback(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    free(data->device_name);
    data->device_name = strdup(ppa_stream_get_device_name(stream));
}//}}}

static void context_state_callback2(pa_context *context, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    if(ppa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        AL_PRINT("Received context failure!\n");
        aluHandleDisconnect(Device);
    }
    ppa_threaded_mainloop_signal(data->loop, 0);
}//}}}

static void stream_state_callback2(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    if(ppa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        AL_PRINT("Received stream failure!\n");
        aluHandleDisconnect(Device);
    }
    ppa_threaded_mainloop_signal(data->loop, 0);
}//}}}

static void stream_success_callback(pa_stream *stream, int success, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;
    (void)stream;
    (void)success;

    ppa_threaded_mainloop_signal(data->loop, 0);
}//}}}

static void sink_info_callback(pa_context *context, const pa_sink_info *info, int eol, void *pdata) //{{{
{
    ALCdevice *device = pdata;
    pulse_data *data = device->ExtraData;
    char chanmap_str[256] = "";
    const struct {
        const char *str;
        ALenum format;
    } chanmaps[] = {
        { "front-left,front-right,front-center,lfe,rear-left,rear-right,side-left,side-right",
          AL_FORMAT_71CHN32 },
        { "front-left,front-right,front-center,lfe,rear-center,side-left,side-right",
          AL_FORMAT_61CHN32 },
        { "front-left,front-right,front-center,lfe,rear-left,rear-right",
          AL_FORMAT_51CHN32 },
        { "front-left,front-right,rear-left,rear-right", AL_FORMAT_QUAD32 },
        { "front-left,front-right", AL_FORMAT_STEREO_FLOAT32 },
        { "mono", AL_FORMAT_MONO_FLOAT32 },
        { NULL, 0 }
    };
    int i;
    (void)context;

    if(eol)
    {
        ppa_threaded_mainloop_signal(data->loop, 0);
        return;
    }

    for(i = 0;chanmaps[i].str;i++)
    {
        pa_channel_map map;
        if(!ppa_channel_map_parse(&map, chanmaps[i].str))
            continue;

        if(ppa_channel_map_equal(&info->channel_map, &map)
#if PA_CHECK_VERSION(0,9,15)
           || (ppa_channel_map_superset &&
               ppa_channel_map_superset(&info->channel_map, &map))
#endif
            )
        {
            device->Format = chanmaps[i].format;
            return;
        }
    }

    ppa_channel_map_snprint(chanmap_str, sizeof(chanmap_str), &info->channel_map);
    AL_PRINT("Failed to find format for channel map:\n    %s\n", chanmap_str);
}//}}}

static void sink_device_callback(pa_context *context, const pa_sink_info *info, int eol, void *pdata) //{{{
{
    pa_threaded_mainloop *loop = pdata;
    void *temp;

    (void)context;

    if(eol)
    {
        ppa_threaded_mainloop_signal(loop, 0);
        return;
    }

    temp = realloc(allDevNameMap, (numDevNames+1) * sizeof(*allDevNameMap));
    if(temp)
    {
        char str[256];
        snprintf(str, sizeof(str), "PulseAudio on %s", info->description);

        allDevNameMap = temp;
        allDevNameMap[numDevNames].name = strdup(str);
        allDevNameMap[numDevNames].device_name = strdup(info->name);
        numDevNames++;
    }
}//}}}

static void source_device_callback(pa_context *context, const pa_source_info *info, int eol, void *pdata) //{{{
{
    pa_threaded_mainloop *loop = pdata;
    void *temp;

    (void)context;

    if(eol)
    {
        ppa_threaded_mainloop_signal(loop, 0);
        return;
    }

    temp = realloc(allCaptureDevNameMap, (numCaptureDevNames+1) * sizeof(*allCaptureDevNameMap));
    if(temp)
    {
        char str[256];
        snprintf(str, sizeof(str), "PulseAudio on %s", info->description);

        allCaptureDevNameMap = temp;
        allCaptureDevNameMap[numCaptureDevNames].name = strdup(str);
        allCaptureDevNameMap[numCaptureDevNames].device_name = strdup(info->name);
        numCaptureDevNames++;
    }
}//}}}
//}}}

// PulseAudio I/O Callbacks //{{{
static void stream_write_callback(pa_stream *stream, size_t len, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    while(len > 0)
    {
        size_t newlen = len;
        void *buf;
        pa_free_cb_t free_func = NULL;

#if PA_CHECK_VERSION(0,9,16)
        if(!ppa_stream_begin_write ||
           ppa_stream_begin_write(stream, &buf, &newlen) < 0)
#endif
        {
            buf = ppa_xmalloc(newlen);
            free_func = ppa_xfree;
        }

        aluMixData(Device, buf, newlen/data->frame_size);
        ppa_stream_write(stream, buf, newlen, free_func, 0, PA_SEEK_RELATIVE);
        len -= newlen;
    }
} //}}}
//}}}

static pa_context *connect_context(pa_threaded_mainloop *loop)
{
    const char *name = "OpenAL Soft";
    char path_name[PATH_MAX];
    pa_context_state_t state;
    pa_context *context;
    int err;

    if(ppa_get_binary_name(path_name, sizeof(path_name)))
        name = ppa_path_get_filename(path_name);

    context = ppa_context_new(ppa_threaded_mainloop_get_api(loop), name);
    if(!context)
    {
        AL_PRINT("pa_context_new() failed\n");
        return NULL;
    }

    ppa_context_set_state_callback(context, context_state_callback, loop);

    if((err=ppa_context_connect(context, NULL, pulse_ctx_flags, NULL)) >= 0)
    {
        while((state=ppa_context_get_state(context)) != PA_CONTEXT_READY)
        {
            if(!PA_CONTEXT_IS_GOOD(state))
            {
                err = ppa_context_errno(context);
                break;
            }

            ppa_threaded_mainloop_wait(loop);
        }
    }
    ppa_context_set_state_callback(context, NULL, NULL);

    if(err < 0)
    {
        AL_PRINT("Context did not connect: %s\n", ppa_strerror(err));
        ppa_context_unref(context);
        return NULL;
    }

    return context;
}

static pa_stream *connect_playback_stream(ALCdevice *device,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap)
{
    pulse_data *data = device->ExtraData;
    pa_stream_state_t state;
    pa_stream *stream;

    stream = ppa_stream_new(data->context, "Playback Stream", spec, chanmap);
    if(!stream)
    {
        AL_PRINT("pa_stream_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));
        return NULL;
    }

    ppa_stream_set_state_callback(stream, stream_state_callback, data->loop);

    if(ppa_stream_connect_playback(stream, data->device_name, attr, flags, NULL, NULL) < 0)
    {
        AL_PRINT("Stream did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));
        ppa_stream_unref(stream);
        return NULL;
    }

    while((state=ppa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            AL_PRINT("Stream did not get ready: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));
            ppa_stream_unref(stream);
            return NULL;
        }

        ppa_threaded_mainloop_wait(data->loop);
    }
    ppa_stream_set_state_callback(stream, NULL, NULL);

    return stream;
}

static void probe_devices(ALboolean capture)
{
    pa_threaded_mainloop *loop;

    if((loop=ppa_threaded_mainloop_new()) &&
       ppa_threaded_mainloop_start(loop) >= 0)
    {
        pa_context *context;

        ppa_threaded_mainloop_lock(loop);
        context = connect_context(loop);
        if(context)
        {
            pa_operation *o;

            if(capture == AL_FALSE)
            {
                allDevNameMap = malloc(sizeof(DevMap) * 1);
                allDevNameMap[0].name = strdup("PulseAudio on default");
                allDevNameMap[0].device_name = NULL;
                numDevNames = 1;
                o = ppa_context_get_sink_info_list(context, sink_device_callback, loop);
            }
            else
            {
                allCaptureDevNameMap = malloc(sizeof(DevMap) * 1);
                allCaptureDevNameMap[0].name = strdup("PulseAudio Capture");
                allCaptureDevNameMap[0].device_name = NULL;
                numCaptureDevNames = 1;
                o = ppa_context_get_source_info_list(context, source_device_callback, loop);
            }
            while(ppa_operation_get_state(o) == PA_OPERATION_RUNNING)
                ppa_threaded_mainloop_wait(loop);
            ppa_operation_unref(o);

            ppa_context_disconnect(context);
            ppa_context_unref(context);
        }
        ppa_threaded_mainloop_unlock(loop);
    }
    if(loop)
    {
        ppa_threaded_mainloop_stop(loop);
        ppa_threaded_mainloop_free(loop);
    }
}


static ALCboolean pulse_open(ALCdevice *device, const ALCchar *device_name) //{{{
{
    pulse_data *data = ppa_xmalloc(sizeof(pulse_data));
    memset(data, 0, sizeof(*data));

    if(!(data->loop = ppa_threaded_mainloop_new()))
    {
        AL_PRINT("pa_threaded_mainloop_new() failed!\n");
        goto out;
    }
    if(ppa_threaded_mainloop_start(data->loop) < 0)
    {
        AL_PRINT("pa_threaded_mainloop_start() failed\n");
        goto out;
    }

    ppa_threaded_mainloop_lock(data->loop);
    device->ExtraData = data;

    data->context = connect_context(data->loop);
    if(!data->context)
    {
        ppa_threaded_mainloop_unlock(data->loop);
        goto out;
    }
    ppa_context_set_state_callback(data->context, context_state_callback2, device);

    device->szDeviceName = strdup(device_name);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;

out:
    if(data->loop)
    {
        ppa_threaded_mainloop_stop(data->loop);
        ppa_threaded_mainloop_free(data->loop);
    }

    device->ExtraData = NULL;
    ppa_xfree(data);
    return ALC_FALSE;
} //}}}

static void pulse_close(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);

    if(data->stream)
    {
        ppa_stream_disconnect(data->stream);
        ppa_stream_unref(data->stream);
    }

    ppa_context_disconnect(data->context);
    ppa_context_unref(data->context);

    ppa_threaded_mainloop_unlock(data->loop);

    ppa_threaded_mainloop_stop(data->loop);
    ppa_threaded_mainloop_free(data->loop);

    DestroyRingBuffer(data->ring);
    free(data->device_name);

    device->ExtraData = NULL;
    ppa_xfree(data);
} //}}}
//}}}

// OpenAL {{{
static ALCboolean pulse_open_playback(ALCdevice *device, const ALCchar *device_name) //{{{
{
    char *pulse_name = NULL;
    pa_sample_spec spec;
    pulse_data *data;
    ALuint len;

    if(!pulse_load())
        return ALC_FALSE;

    if(!device_name)
        device_name = pulse_device;
    else if(strcmp(device_name, pulse_device) != 0)
    {
        ALuint i;

        if(!allDevNameMap)
            probe_devices(AL_FALSE);

        for(i = 0;i < numDevNames;i++)
        {
            if(strcmp(device_name, allDevNameMap[i].name) == 0)
            {
                pulse_name = allDevNameMap[i].device_name;
                break;
            }
        }
        if(i == numDevNames)
            return ALC_FALSE;
    }

    if(pulse_open(device, device_name) == ALC_FALSE)
        return ALC_FALSE;

    data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);

    spec.format = PA_SAMPLE_S16NE;
    spec.rate = 44100;
    spec.channels = 2;

    data->device_name = pulse_name;
    pa_stream *stream = connect_playback_stream(device, 0, NULL, &spec, NULL);
    if(!stream)
    {
        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    if(ppa_stream_is_suspended(stream))
    {
        ppa_stream_disconnect(stream);
        ppa_stream_unref(stream);
        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }
    data->device_name = strdup(ppa_stream_get_device_name(stream));

    ppa_stream_disconnect(stream);
    ppa_stream_unref(stream);

    ppa_threaded_mainloop_unlock(data->loop);

    len = GetConfigValueInt("pulse", "buffer-length", 2048);
    if(len != 0)
    {
        device->UpdateSize = len;
        device->NumUpdates = 1;
    }
    return ALC_TRUE;

fail:
    pulse_close(device);
    return ALC_FALSE;
} //}}}

static void pulse_close_playback(ALCdevice *device) //{{{
{
    pulse_close(device);
} //}}}

static ALCboolean pulse_reset_playback(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    pa_stream_flags_t flags = 0;
    pa_channel_map chanmap;

    ppa_threaded_mainloop_lock(data->loop);

    if(!ConfigValueExists(NULL, "format"))
    {
        pa_operation *o;
        o = ppa_context_get_sink_info_by_name(data->context, data->device_name, sink_info_callback, device);
        while(ppa_operation_get_state(o) == PA_OPERATION_RUNNING)
            ppa_threaded_mainloop_wait(data->loop);
        ppa_operation_unref(o);
    }
    if(!ConfigValueExists(NULL, "frequency"))
        flags |= PA_STREAM_FIX_RATE;

    data->frame_size = aluFrameSizeFromFormat(device->Format);
    data->attr.minreq = -1;
    data->attr.prebuf = -1;
    data->attr.fragsize = -1;
    data->attr.tlength = device->UpdateSize * device->NumUpdates *
                         data->frame_size;
    data->attr.maxlength = data->attr.tlength;

    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            data->spec.format = PA_SAMPLE_U8;
            break;
        case 2:
            data->spec.format = PA_SAMPLE_S16NE;
            break;
        case 4:
            data->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
        default:
            AL_PRINT("Unknown format: 0x%x\n", device->Format);
            ppa_threaded_mainloop_unlock(data->loop);
            return ALC_FALSE;
    }
    data->spec.rate = device->Frequency;
    data->spec.channels = aluChannelsFromFormat(device->Format);

    if(ppa_sample_spec_valid(&data->spec) == 0)
    {
        AL_PRINT("Invalid sample format\n");
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }

    if(!ppa_channel_map_init_auto(&chanmap, data->spec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        AL_PRINT("Couldn't build map for channel count (%d)!\n", data->spec.channels);
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    data->stream = connect_playback_stream(device, flags, &data->attr, &data->spec, &chanmap);
    if(!data->stream)
    {
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }

    ppa_stream_set_state_callback(data->stream, stream_state_callback2, device);

    data->spec = *(ppa_stream_get_sample_spec(data->stream));
    if(device->Frequency != data->spec.rate)
    {
        pa_operation *o;

        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly. */
        data->attr.tlength = (ALuint64)(data->attr.tlength/data->frame_size) *
                             data->spec.rate / device->Frequency * data->frame_size;
        data->attr.maxlength = data->attr.tlength;

        o = ppa_stream_set_buffer_attr(data->stream, &data->attr,
                                       stream_success_callback, device);
        while(ppa_operation_get_state(o) == PA_OPERATION_RUNNING)
            ppa_threaded_mainloop_wait(data->loop);
        ppa_operation_unref(o);

        device->Frequency = data->spec.rate;
    }

    stream_buffer_attr_callback(data->stream, device);
#if PA_CHECK_VERSION(0,9,15)
    if(ppa_stream_set_buffer_attr_callback)
        ppa_stream_set_buffer_attr_callback(data->stream, stream_buffer_attr_callback, device);
#endif
    ppa_stream_set_moved_callback(data->stream, stream_device_callback, device);

    stream_write_callback(data->stream, data->attr.tlength, device);
    ppa_stream_set_write_callback(data->stream, stream_write_callback, device);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;
} //}}}

static void pulse_stop_playback(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    if(!data->stream)
        return;

    ppa_threaded_mainloop_lock(data->loop);

#if PA_CHECK_VERSION(0,9,15)
    if(ppa_stream_set_buffer_attr_callback)
        ppa_stream_set_buffer_attr_callback(data->stream, NULL, NULL);
#endif
    ppa_stream_set_moved_callback(data->stream, NULL, NULL);
    ppa_stream_set_write_callback(data->stream, NULL, NULL);
    ppa_stream_disconnect(data->stream);
    ppa_stream_unref(data->stream);
    data->stream = NULL;

    ppa_threaded_mainloop_unlock(data->loop);
} //}}}


static ALCboolean pulse_open_capture(ALCdevice *device, const ALCchar *device_name) //{{{
{
    char *pulse_name = NULL;
    pulse_data *data;
    pa_stream_flags_t flags = 0;
    pa_stream_state_t state;
    pa_channel_map chanmap;

    if(!pulse_load())
        return ALC_FALSE;

    if(!allCaptureDevNameMap)
        probe_devices(AL_TRUE);

    if(!device_name)
        device_name = allCaptureDevNameMap[0].name;
    else
    {
        ALuint i;

        for(i = 0;i < numCaptureDevNames;i++)
        {
            if(strcmp(device_name, allCaptureDevNameMap[i].name) == 0)
            {
                pulse_name = allCaptureDevNameMap[i].device_name;
                break;
            }
        }
        if(i == numCaptureDevNames)
            return ALC_FALSE;
    }

    if(pulse_open(device, device_name) == ALC_FALSE)
        return ALC_FALSE;

    data = device->ExtraData;
    ppa_threaded_mainloop_lock(data->loop);

    data->samples = device->UpdateSize * device->NumUpdates;
    data->frame_size = aluFrameSizeFromFormat(device->Format);

    if(!(data->ring = CreateRingBuffer(data->frame_size, data->samples)))
    {
        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    data->attr.minreq = -1;
    data->attr.prebuf = -1;
    data->attr.maxlength = data->frame_size * data->samples;
    data->attr.tlength = -1;
    data->attr.fragsize = min(data->frame_size * data->samples,
                              10 * device->Frequency / 1000);

    data->spec.rate = device->Frequency;
    data->spec.channels = aluChannelsFromFormat(device->Format);

    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            data->spec.format = PA_SAMPLE_U8;
            break;
        case 2:
            data->spec.format = PA_SAMPLE_S16NE;
            break;
        case 4:
            data->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
        default:
            AL_PRINT("Unknown format: 0x%x\n", device->Format);
            ppa_threaded_mainloop_unlock(data->loop);
            goto fail;
    }

    if(ppa_sample_spec_valid(&data->spec) == 0)
    {
        AL_PRINT("Invalid sample format\n");
        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    if(!ppa_channel_map_init_auto(&chanmap, data->spec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        AL_PRINT("Couldn't build map for channel count (%d)!\n", data->spec.channels);
        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    data->stream = ppa_stream_new(data->context, "Capture Stream", &data->spec, &chanmap);
    if(!data->stream)
    {
        AL_PRINT("pa_stream_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    ppa_stream_set_state_callback(data->stream, stream_state_callback, data->loop);

    flags |= PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY;
    if(ppa_stream_connect_record(data->stream, pulse_name, &data->attr, flags) < 0)
    {
        AL_PRINT("Stream did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_stream_unref(data->stream);
        data->stream = NULL;

        ppa_threaded_mainloop_unlock(data->loop);
        goto fail;
    }

    while((state=ppa_stream_get_state(data->stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            AL_PRINT("Stream did not get ready: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_stream_unref(data->stream);
            data->stream = NULL;

            ppa_threaded_mainloop_unlock(data->loop);
            goto fail;
        }

        ppa_threaded_mainloop_wait(data->loop);
    }
    ppa_stream_set_state_callback(data->stream, stream_state_callback2, device);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;

fail:
    pulse_close(device);
    return ALC_FALSE;
} //}}}

static void pulse_close_capture(ALCdevice *device) //{{{
{
    pulse_close(device);
} //}}}

static void pulse_start_capture(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    pa_operation *o;

    ppa_threaded_mainloop_lock(data->loop);
    o = ppa_stream_cork(data->stream, 0, stream_success_callback, device);
    while(ppa_operation_get_state(o) == PA_OPERATION_RUNNING)
        ppa_threaded_mainloop_wait(data->loop);
    ppa_operation_unref(o);
    ppa_threaded_mainloop_unlock(data->loop);
} //}}}

static void pulse_stop_capture(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    pa_operation *o;

    ppa_threaded_mainloop_lock(data->loop);
    o = ppa_stream_cork(data->stream, 1, stream_success_callback, device);
    while(ppa_operation_get_state(o) == PA_OPERATION_RUNNING)
        ppa_threaded_mainloop_wait(data->loop);
    ppa_operation_unref(o);
    ppa_threaded_mainloop_unlock(data->loop);
} //}}}

static void pulse_capture_samples(ALCdevice *device, ALCvoid *buffer, ALCuint samples) //{{{
{
    pulse_data *data = device->ExtraData;
    ALCuint available = RingBufferSize(data->ring);
    const void *buf;
    size_t length;

    available *= data->frame_size;
    samples *= data->frame_size;

    ppa_threaded_mainloop_lock(data->loop);
    if(available+ppa_stream_readable_size(data->stream) < samples)
    {
        ppa_threaded_mainloop_unlock(data->loop);
        alcSetError(device, ALC_INVALID_VALUE);
        return;
    }

    available = min(available, samples);
    if(available > 0)
    {
        ReadRingBuffer(data->ring, buffer, available/data->frame_size);
        buffer = (ALubyte*)buffer + available;
        samples -= available;
    }

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's requested */
    while(samples > 0)
    {
        if(ppa_stream_peek(data->stream, &buf, &length) < 0)
        {
            AL_PRINT("pa_stream_peek() failed: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));
            break;
        }
        available = min(length, samples);

        memcpy(buffer, buf, available);
        buffer = (ALubyte*)buffer + available;
        buf = (const ALubyte*)buf + available;
        samples -= available;
        length -= available;

        /* Any unread data in the fragment will be lost, so save it */
        length /= data->frame_size;
        if(length > 0)
        {
            if(length > data->samples)
                length = data->samples;
            WriteRingBuffer(data->ring, buf, length);
        }

        ppa_stream_drop(data->stream);
    }
    ppa_threaded_mainloop_unlock(data->loop);
} //}}}

static ALCuint pulse_available_samples(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    ALCuint ret;

    ppa_threaded_mainloop_lock(data->loop);
    ret  = RingBufferSize(data->ring);
    ret += ppa_stream_readable_size(data->stream)/data->frame_size;
    ppa_threaded_mainloop_unlock(data->loop);

    return ret;
} //}}}

BackendFuncs pulse_funcs = { //{{{
    pulse_open_playback,
    pulse_close_playback,
    pulse_reset_playback,
    pulse_stop_playback,
    pulse_open_capture,
    pulse_close_capture,
    pulse_start_capture,
    pulse_stop_capture,
    pulse_capture_samples,
    pulse_available_samples
}; //}}}

void alc_pulse_init(BackendFuncs *func_list) //{{{
{
    *func_list = pulse_funcs;

    pulse_ctx_flags = 0;
    if(!GetConfigValueBool("pulse", "spawn-server", 0))
        pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;
} //}}}

void alc_pulse_deinit(void) //{{{
{
    ALuint i;

    for(i = 0;i < numDevNames;++i)
    {
        free(allDevNameMap[i].name);
        free(allDevNameMap[i].device_name);
    }
    free(allDevNameMap);
    allDevNameMap = NULL;
    numDevNames = 0;

    for(i = 0;i < numCaptureDevNames;++i)
    {
        free(allCaptureDevNameMap[i].name);
        free(allCaptureDevNameMap[i].device_name);
    }
    free(allCaptureDevNameMap);
    allCaptureDevNameMap = NULL;
    numCaptureDevNames = 0;

    if(pa_handle)
    {
#ifdef _WIN32
        FreeLibrary(pa_handle);
#elif defined (HAVE_DLFCN_H)
        dlclose(pa_handle);
#endif
        pa_handle = NULL;
    }
} //}}}

void alc_pulse_probe(int type) //{{{
{
    if(!pulse_load()) return;

    if(type == DEVICE_PROBE)
        AppendDeviceList(pulse_device);
    else if(type == ALL_DEVICE_PROBE)
    {
        ALuint i;

        for(i = 0;i < numDevNames;++i)
        {
            free(allDevNameMap[i].name);
            free(allDevNameMap[i].device_name);
        }
        free(allDevNameMap);
        allDevNameMap = NULL;
        numDevNames = 0;

        probe_devices(AL_FALSE);

        for(i = 0;i < numDevNames;i++)
            AppendAllDeviceList(allDevNameMap[i].name);
    }
    else if(type == CAPTURE_DEVICE_PROBE)
    {
        ALuint i;

        for(i = 0;i < numCaptureDevNames;++i)
        {
            free(allCaptureDevNameMap[i].name);
            free(allCaptureDevNameMap[i].device_name);
        }
        free(allCaptureDevNameMap);
        allCaptureDevNameMap = NULL;
        numCaptureDevNames = 0;

        probe_devices(AL_TRUE);

        for(i = 0;i < numCaptureDevNames;i++)
            AppendCaptureDeviceList(allCaptureDevNameMap[i].name);
    }
} //}}}
//}}}
