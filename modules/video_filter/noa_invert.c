/*****************************************************************************
 * noa_invert.c : Invert video plugin for vlc
 *****************************************************************************
 * Copyright (C) 2000-2006 VLC authors and VideoLAN
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
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

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

static int  Create      ( vlc_object_t * );
static void Destroy     ( vlc_object_t * );

static picture_t *Filter( filter_t *, picture_t * );

static const char *const ppsz_filter_options[] = {
    "active", NULL
};

typedef struct
{
    atomic_bool b_active;
    filter_chain_t *p_chain;
    filter_t *p_scale_filter;

    // user callback data
    void* cb_opaque;
    void (*cb_greet)(void* opaque, const char* name);
} filter_sys_t;

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

#define INVERT_ACIVE_TEXT N_("Invert active")
#define INVERT_ACTIVE_LONGTEXT N_("Whether the invert effect is active or not")

#define CFG_PREFIX "invert-"

vlc_module_begin ()
    set_description( N_("NOA invert video filter") )
    set_shortname( N_("Color inversion" ))
    set_category( CAT_VIDEO )
    set_subcategory( SUBCAT_VIDEO_VFILTER )

    set_capability( "video filter", 0 )
    add_shortcut( "noa_invert" )
    add_bool( CFG_PREFIX "active", false, INVERT_ACIVE_TEXT, INVERT_ACTIVE_LONGTEXT, false )
    set_callbacks( Create, Destroy )
vlc_module_end ()

/*****************************************************************************
 * Buffer management
 *****************************************************************************/

static picture_t *BufferNew( filter_t *p_filter )
{
    filter_t *p_parent = p_filter->owner.sys;

    return filter_NewPicture( p_parent );
}

static const struct filter_video_callbacks filter_video_chain_cbs =
{
    .buffer_new = BufferNew,
};

/*****************************************************************************
 * Create: initialize filter
 *****************************************************************************/

static int Create( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;
    filter_sys_t *p_sys;

    p_sys = malloc( sizeof( filter_sys_t ) );
    if( unlikely( p_sys == NULL ) )
        return VLC_ENOMEM;
    p_filter->p_sys = p_sys;

    p_sys->b_active = false;
    config_ChainParse( p_filter, CFG_PREFIX, ppsz_filter_options, p_filter->p_cfg );
    atomic_init( &p_sys->b_active,
        var_CreateGetBoolCommand( p_filter, CFG_PREFIX "active" ) );
    p_sys->cb_opaque = var_InheritAddress( p_filter, "noa-hello-cb-opaque" );
    p_sys->cb_greet = var_InheritAddress( p_filter, "noa-hello-cb-greet" );

    filter_owner_t owner = {
        .video = &filter_video_chain_cbs,
        .sys = p_filter,
    };

    p_sys->p_chain = filter_chain_NewVideo( p_filter, false, &owner );
    if( !p_sys->p_chain ) {
        free( p_sys );
        return VLC_EGENERIC;
    }

    p_sys->p_scale_filter = filter_chain_AppendFilter( p_sys->p_chain,
        "invert", p_filter->p_cfg, &p_filter->fmt_in, &p_filter->fmt_out );

    if( p_sys->cb_greet != NULL )
    {
        const char *greet = p_sys->b_active ? "Good dag" : "Good natt";
        p_sys->cb_greet(p_sys->cb_opaque, greet);
    }

    es_format_Copy(&p_filter->fmt_out, &p_filter->fmt_in);
    p_filter->pf_video_filter = Filter;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Destroy: uninitialize filter
 *****************************************************************************/

static void Destroy( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t*)p_this;
    filter_sys_t *p_sys = p_filter->p_sys;

    p_sys->cb_opaque = NULL;
    p_sys->cb_greet = NULL;
    filter_chain_Delete( p_sys->p_chain );
    free( p_sys );
}

/*****************************************************************************
 * Filter: process picture buffer
 *****************************************************************************/

static picture_t *Filter( filter_t *p_filter, picture_t *p_pic )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    picture_t *p_outpic;

    if( !p_pic ) return NULL;

    if( !p_sys->b_active )
        return p_pic;

    p_outpic = filter_chain_VideoFilter( p_sys->p_chain, p_pic );
    return p_outpic;
}
