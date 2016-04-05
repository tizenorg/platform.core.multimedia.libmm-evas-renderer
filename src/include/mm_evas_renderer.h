/*
 * libmm-evas-renderer
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: JongHyuk Choi <jhchoi.choi@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef __TIZEN_MEDIA_EVASRENDERER_H__
#define __TIZEN_MEDIA_EVASRENDERER_H__

/*===========================================================================================
|																							|
|  INCLUDE FILES																			|
|																							|
========================================================================================== */
#include <stdio.h>

#include <Evas.h>
#include <Ecore.h>

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>

#include "mm_types.h"
#include "mm_debug.h"

#include <media/media_packet.h>
#include <media/media_format.h>

#define MAX_PACKET_NUM 20

/*===========================================================================================
|																							|
|  GLOBAL DEFINITIONS AND DECLARATIONS FOR MODULE											|
|																							|
========================================================================================== */

/*---------------------------------------------------------------------------
|    GLOBAL #defines:														|
---------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	UPDATE_VISIBILITY,
	UPDATE_TBM_SURF
} update_info;

typedef enum {
	VISIBLE_NONE,			/* if user dont set visibility, it will be changed to true */
	VISIBLE_TRUE,
	VISIBLE_FALSE
} visible_info;

typedef struct {
	media_packet_h packet;
	tbm_surface_h tbm_surf;
	gint prev; /* keep previous index for destroying remained packets */
} packet_info;

typedef struct {
	gint x;
	gint y;
	gint w;
	gint h;
} rect_info;

/* evas info for evas surface type */
typedef struct {
	Evas_Object *eo;
	Ecore_Pipe *epipe;

	/* video width & height */
	gint w;
	gint h;

	/* properties */
	visible_info visible;
	rect_info eo_size;
	gboolean use_ratio;
	guint rotate_angle;
	guint display_geometry_method;
	guint flip;

	tbm_surface_h tbm_surf;

	/* media packet */
	packet_info pkt_info[MAX_PACKET_NUM];
	gint cur_idx;

	/* count undestroyed media packet */
	guint sent_buffer_cnt;

	/* lock */
	GMutex mp_lock; /* media_packet free lock */
	GMutex idx_lock; /* to keep value of cur_idx */
} mm_evas_info;

/* create and initialize evas_info */
int mm_evas_renderer_create(MMHandleType *handle, Evas_Object *eo);
/* destroy and finalize evas_info */
int mm_evas_renderer_destroy(MMHandleType *handle);
/* set and get visibility value */
int mm_evas_renderer_set_visible(MMHandleType handle, bool visible);
int mm_evas_renderer_get_visible(MMHandleType handle, bool *visible);
/* set and get rotation value */
int mm_evas_renderer_set_rotation(MMHandleType handle, int rotate);
int mm_evas_renderer_get_rotation(MMHandleType handle, int *rotate);
/* set and get geometry value */
int mm_evas_renderer_set_geometry(MMHandleType handle, int mode);
int mm_evas_renderer_get_geometry(MMHandleType handle, int *mode);
/* update all properties */
int mm_evas_renderer_update_param(MMHandleType handle);
/* call ecore_pipe_write, when packet is sent */
void mm_evas_renderer_write(media_packet_h packet, void *data);

#ifdef __cplusplus
}
#endif
#endif							/* __TIZEN_MEDIA_EVASRENDERER_H__ */
