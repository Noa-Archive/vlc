/*****************************************************************************
 * noa_hello.c : example filter
 *****************************************************************************
 * Copyright © 2022 NOA GmbH
 *
 * Authors: Tobias Rapp <t.rapp@noa-archive.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_aout.h>
#include <vlc_aout_volume.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

static int      Open        ( vlc_object_t * );
static void     Close       ( vlc_object_t * );
static block_t  *Process    ( filter_t *, block_t * );

typedef struct
{
    // buffer for storing min/max peak values
    float f_peaks_min[INPUT_CHAN_MAX];
    float f_peaks_max[INPUT_CHAN_MAX];
    // function for calculating peak values depending on the input format
    void (*get_peaks)(block_t *p_block, int i_channels, float *f_min, float *f_max);
    // user callback data
    void* cb_opaque;
    void (*cb_greet)(void* opaque, const char* name);
    void (*cb_peaks)(void* opaque, vlc_tick_t pts, int channels, float *min, float *max);
} filter_sys_t;

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin()
    set_shortname( N_("Hello") )
    set_description( N_("Hello example filter") )
    set_category( CAT_AUDIO )
    set_subcategory( SUBCAT_AUDIO_AFILTER )

    set_capability( "audio filter", 0 )
    set_callbacks( Open, Close )
vlc_module_end()

/*****************************************************************************
 * Helper functions
 *****************************************************************************/

static void GetPeaksFL32(block_t *p_block, int i_channels, float *f_min, float *f_max)
{
    float *p_sample = (float *)p_block->p_buffer;

    for (int ch = 0; ch < i_channels; ch++) {
        f_min[ch] = 0.0;
        f_max[ch] = 0.0;
    }

    for (size_t i = 0; i < p_block->i_nb_samples; i++) {
        for (int ch = 0; ch < i_channels; ch++) {
            float val = *p_sample++;
            if (val < f_min[ch])
                f_min[ch] = val;
            if (val > f_max[ch])
                f_max[ch] = val;
        }
    }
}

static void GetPeaksFL64(block_t *p_block, int i_channels, float *f_min, float *f_max)
{
    double *p_sample = (double *)p_block->p_buffer;
    double f_native_min[INPUT_CHAN_MAX] = { 0.0 };
    double f_native_max[INPUT_CHAN_MAX] = { 0.0 };

    assert(i_channels <= INPUT_CHAN_MAX);
    for (size_t i = 0; i < p_block->i_nb_samples; i++) {
        for (int ch = 0; ch < i_channels; ch++) {
            double val = *p_sample++;
            if (val < f_native_min[ch])
                f_native_min[ch] = val;
            if (val > f_native_max[ch])
                f_native_max[ch] = val;
        }
    }

    for (int ch = 0; ch < i_channels; ch++) {
        f_min[ch] = (float)f_native_min[ch];
        f_max[ch] = (float)f_native_max[ch];
    }
}

static void GetPeaksS16N(block_t *p_block, int i_channels, float *f_min, float *f_max)
{
    int16_t *p_sample = (int16_t *)p_block->p_buffer;
    int16_t i_native_min[INPUT_CHAN_MAX] = { 0 };
    int16_t i_native_max[INPUT_CHAN_MAX] = { 0 };

    assert(i_channels <= INPUT_CHAN_MAX);
    for (size_t i = 0; i < p_block->i_nb_samples; i++) {
        for (int ch = 0; ch < i_channels; ch++) {
            int16_t val = *p_sample++;
            if (val < i_native_min[ch])
                i_native_min[ch] = val;
            if (val > i_native_max[ch])
                i_native_max[ch] = val;
        }
    }

    for (int ch = 0; ch < i_channels; ch++) {
        f_min[ch] = (float)i_native_min[ch] / 0x1.p15f; // 2^15 as float
        f_max[ch] = (float)i_native_max[ch] / 0x1.p15f; // 2^15 as float
    }
}

static void GetPeaksS32N(block_t *p_block, int i_channels, float *f_min, float *f_max)
{
    int32_t *p_sample = (int32_t *)p_block->p_buffer;
    int32_t i_native_min[INPUT_CHAN_MAX] = { 0 };
    int32_t i_native_max[INPUT_CHAN_MAX] = { 0 };

    assert(i_channels <= INPUT_CHAN_MAX);
    for (size_t i = 0; i < p_block->i_nb_samples; i++) {
        for (int ch = 0; ch < i_channels; ch++) {
            int32_t val = *p_sample++;
            if (val < i_native_min[ch])
                i_native_min[ch] = val;
            if (val > i_native_max[ch])
                i_native_max[ch] = val;
        }
    }

    for (int ch = 0; ch < i_channels; ch++) {
        f_min[ch] = (float)i_native_min[ch] / 0x1.p31f; // 2^31 as float
        f_max[ch] = (float)i_native_max[ch] / 0x1.p31f; // 2^31 as float
    }
}

/*****************************************************************************
 * Open: initialize filter
 *****************************************************************************/

static int Open( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    filter_sys_t *p_sys = vlc_object_create( p_this, sizeof( *p_sys ) );
    if( unlikely( p_sys == NULL ) )
        return VLC_ENOMEM;

    p_filter->p_sys = p_sys;
    switch (p_filter->fmt_in.audio.i_format)
    {
        case VLC_CODEC_FL32:
            p_sys->get_peaks = GetPeaksFL32;
            break;
        case VLC_CODEC_FL64:
            p_sys->get_peaks = GetPeaksFL64;
            break;
        case VLC_CODEC_S16N:
            p_sys->get_peaks = GetPeaksS16N;
            break;
        case VLC_CODEC_S32N:
            p_sys->get_peaks = GetPeaksS32N;
            break;
        default:
            msg_Warn( p_filter, "unsupported input format" );
            p_sys->get_peaks = NULL;
    }

    p_sys->cb_opaque = var_InheritAddress( p_filter, "noa-hello-cb-opaque" );
    p_sys->cb_greet = var_InheritAddress( p_filter, "noa-hello-cb-greet" );
    p_sys->cb_peaks = var_InheritAddress( p_filter, "noa-hello-cb-peaks" );

    if( p_sys->cb_greet != NULL )
        p_sys->cb_greet(p_sys->cb_opaque, "Hello world!");

    p_filter->fmt_out.audio = p_filter->fmt_in.audio;
    p_filter->pf_audio_filter = Process;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Process: process samples buffer
 *****************************************************************************/

static block_t *Process( filter_t *p_filter, block_t *p_block )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    int i_channels = aout_FormatNbChannels(&p_filter->fmt_in.audio);

    if( p_sys->get_peaks != NULL ) {
        p_sys->get_peaks(p_block, i_channels, p_sys->f_peaks_min, p_sys->f_peaks_max);
    } else {
        // indicate to the following callback that no peak data has been processed
        i_channels = 0;
    }
    if( p_sys->cb_peaks ) {
        p_sys->cb_peaks(p_sys->cb_opaque, p_block->i_pts, i_channels, p_sys->f_peaks_min, p_sys->f_peaks_max);
    }

    return p_block;
}

/*****************************************************************************
 * Close: close filter
 *****************************************************************************/

static void Close( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t*)p_this;
    filter_sys_t *p_sys = p_filter->p_sys;

    p_sys->get_peaks = NULL;
    p_sys->cb_opaque = NULL;
    p_sys->cb_greet = NULL;
    p_sys->cb_peaks = NULL;
}
