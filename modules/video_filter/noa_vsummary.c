/*****************************************************************************
 * noa_vsummary.c : Video summary plugin for vlc
 *****************************************************************************
 * Copyright (C) 2022 NOA GmbH
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
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>

#include "filter_picture.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

static int        Open      ( vlc_object_t * );
static void       Close     ( vlc_object_t * );
static picture_t  *Process  ( filter_t *, picture_t * );

typedef struct
{
    filter_chain_t *p_chain;
    // user callback data
    void* cb_opaque;
    void (*cb_picture)(void* opaque, vlc_tick_t pts, int planes, uint8_t **pixels, int *lines, int *widths, int *pitches);
} filter_sys_t;

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin()
    set_shortname( N_("Video Summary") )
    set_description( N_("NOA video summary filter") )
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )

    set_capability( "video filter", 0 )
    add_shortcut( "noa_vsummary" )

    set_callbacks( Open, Close )
vlc_module_end()

/*****************************************************************************
 * Buffer management
 *****************************************************************************/

static picture_t *BufferNew( filter_t *p_filter )
{
    return picture_NewFromFormat( &p_filter->fmt_out.video );
}

static const struct filter_video_callbacks filter_video_chain_cbs =
{
    .buffer_new = BufferNew,
};

/*****************************************************************************
 * Open: initialize filter
 *****************************************************************************/

static int Open( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    filter_sys_t *p_sys;
    es_format_t scale_fmt;
    vlc_fourcc_t i_chroma;
    unsigned int i_width, i_height;
    int ret;

    p_sys = malloc( sizeof( filter_sys_t ) );
    if( unlikely( p_sys == NULL ) )
        return VLC_ENOMEM;
    p_filter->p_sys = p_sys;

    filter_owner_t owner = {
        .video = &filter_video_chain_cbs,
        .sys = p_filter,
    };

    p_sys->p_chain = filter_chain_NewVideo( p_filter, false, &owner );
    if( !p_sys->p_chain ) {
        free( p_sys );
        return VLC_EGENERIC;
    }

    char *chroma = var_InheritString( p_filter, "noa-vsummary-chroma" );
    i_chroma = vlc_fourcc_GetCodecFromString(VIDEO_ES, chroma);
    free( chroma );
    if( !i_chroma ) {
        i_chroma = p_filter->fmt_out.video.i_chroma;
    }
    i_width  = var_InheritInteger( p_filter, "noa-vsummary-width" );
    i_height = var_InheritInteger( p_filter, "noa-vsummary-height" );
    if( !i_width || !i_height ) {
        i_width = p_filter->fmt_out.video.i_width / 4;
        i_height = p_filter->fmt_out.video.i_height / 4;
    }
    p_sys->cb_opaque = var_InheritAddress( p_filter, "noa-vsummary-cb-opaque" );
    p_sys->cb_picture = var_InheritAddress( p_filter, "noa-vsummary-cb-picture" );

    es_format_Init( &scale_fmt, VIDEO_ES, i_chroma );
    video_format_Setup( &scale_fmt.video, i_chroma, i_width, i_height,
        i_width, i_height, 1, 1 );
    filter_chain_Reset( p_sys->p_chain, &p_filter->fmt_in, &scale_fmt );

    ret = filter_chain_AppendConverter( p_sys->p_chain, &p_filter->fmt_in, &scale_fmt );
    if( ret != 0 ) {
        msg_Warn( p_filter, "can't convert scaled output picture" );
    }
    es_format_Clean(&scale_fmt);

    msg_Warn( p_filter, "Opened NOA video summary filter" );
    es_format_Copy(&p_filter->fmt_out, &p_filter->fmt_in);
    p_filter->pf_video_filter = Process;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: close filter
 *****************************************************************************/

static void Close( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t*)p_this;
    filter_sys_t *p_sys = p_filter->p_sys;

    p_sys->cb_opaque = NULL;
    p_sys->cb_picture = NULL;
    filter_chain_Delete( p_sys->p_chain );
    free( p_sys );
}

/*****************************************************************************
 * Process: process picture buffer
 *****************************************************************************/

static picture_t *Process( filter_t *p_filter, picture_t *p_inpic )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    picture_t *p_outpic;
    uint8_t *pixels[PICTURE_PLANE_MAX] = { 0 };
    int lines[PICTURE_PLANE_MAX] = { 0 };
    int widths[PICTURE_PLANE_MAX] = { 0 };
    int pitches[PICTURE_PLANE_MAX] = { 0 };

    if( p_inpic && p_sys->cb_picture ) {
        picture_t *scaled_picture = filter_chain_VideoFilter( p_sys->p_chain, p_inpic );
        if( scaled_picture ) {
            int planes = MIN(scaled_picture->i_planes, PICTURE_PLANE_MAX);
            for (int i = 0; i < planes; i++) {
                pixels[i] = scaled_picture->p[i].p_pixels;
                lines[i] = scaled_picture->p[i].i_visible_lines;
                widths[i] = scaled_picture->p[i].i_visible_pitch;
                pitches[i] = scaled_picture->p[i].i_pitch;
            }
            p_sys->cb_picture(p_sys->cb_opaque, scaled_picture->date,
                planes, pixels, lines, widths, pitches);
            picture_Release( scaled_picture );
        }
    }

    picture_Hold(p_inpic);
    return p_inpic;
}
