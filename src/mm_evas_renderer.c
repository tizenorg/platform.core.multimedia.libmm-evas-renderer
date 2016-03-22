/*
* Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
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
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <dlog.h>
#include <mm_error.h>

#include "mm_evas_renderer.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "MM_EVAS_RENDER"

#define MM_CHECK_NULL( x_var ) \
if ( ! x_var ) \
{ \
	LOGE("[%s] is NULL\n", #x_var ); \
	return MM_ERROR_INVALID_ARGUMENT; \
}

#define SET_EVAS_OBJECT_EVENT_CALLBACK( x_evas_image_object, x_usr_data ) \
	do \
	{ \
		if (x_evas_image_object) { \
			LOGD("object callback add"); \
			evas_object_event_callback_add (x_evas_image_object, EVAS_CALLBACK_DEL, _evas_del_cb, x_usr_data); \
			evas_object_event_callback_add (x_evas_image_object, EVAS_CALLBACK_RESIZE, _evas_resize_cb, x_usr_data); \
		} \
	} while(0)

#define UNSET_EVAS_OBJECT_EVENT_CALLBACK( x_evas_image_object ) \
	do \
	{ \
		if (x_evas_image_object) { \
			LOGD("object callback del"); \
			evas_object_event_callback_del (x_evas_image_object, EVAS_CALLBACK_DEL, _evas_del_cb); \
			evas_object_event_callback_del (x_evas_image_object, EVAS_CALLBACK_RESIZE, _evas_resize_cb); \
		} \
	} while(0)

#define SET_EVAS_EVENT_CALLBACK( x_evas, x_usr_data ) \
	do \
	{ \
		if (x_evas) { \
			LOGD("callback add... evas_callback_render_pre.. evas : %p evas_info : %p", x_evas, x_usr_data); \
			evas_event_callback_add (x_evas, EVAS_CALLBACK_RENDER_PRE, _evas_render_pre_cb, x_usr_data); \
		} \
	} while(0)

#define UNSET_EVAS_EVENT_CALLBACK( x_evas ) \
	do \
	{ \
		if (x_evas) { \
			LOGD("callback del... evas_callback_render_pre %p", x_evas); \
			evas_event_callback_del (x_evas, EVAS_CALLBACK_RENDER_PRE, _evas_render_pre_cb); \
		} \
	} while(0)

enum {
	DISP_GEO_METHOD_LETTER_BOX = 0,
	DISP_GEO_METHOD_ORIGIN_SIZE,
	DISP_GEO_METHOD_FULL_SCREEN,
	DISP_GEO_METHOD_CROPPED_FULL_SCREEN,
	DISP_GEO_METHOD_ORIGIN_SIZE_OR_LETTER_BOX,
	DISP_GEO_METHOD_NUM,
};

enum {
	DEGREE_0 = 0,
	DEGREE_90,
	DEGREE_180,
	DEGREE_270,
	DEGREE_NUM,
};

enum {
	FLIP_NONE = 0,
	FLIP_HORIZONTAL,
	FLIP_VERTICAL,
	FLIP_BOTH,
	FLIP_NUM,
};

/* internal */
int _mm_evas_renderer_create(mm_evas_info **evas_info);
int _mm_evas_renderer_destroy(mm_evas_info **evas_info);
int _mm_evas_renderer_set_info(mm_evas_info *evas_info, Evas_Object *eo);
int _mm_evas_renderer_reset(mm_evas_info *evas_info);
void _mm_evas_renderer_update_geometry(mm_evas_info *evas_info, rect_info *result);
int _mm_evas_renderer_apply_geometry(mm_evas_info *evas_info);
static void _mm_evas_renderer_set_callback(mm_evas_info *evas_info);
static void _mm_evas_renderer_unset_callback(mm_evas_info *evas_info);

static void _evas_resize_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	int x, y, w, h, ret;
	x = y = w = h = 0;

	mm_evas_info *evas_info = data;
	LOGD("[ENTER]");

	if (!evas_info || !evas_info->eo) {
		return;
	}

	evas_object_geometry_get(evas_info->eo, &x, &y, &w, &h);
	if (!w || !h) {
		LOGW("evas object size (w:%d,h:%d) was not set", w, h);
	} else {
		evas_info->eo_size.x = x;
		evas_info->eo_size.y = y;
		evas_info->eo_size.w = w;
		evas_info->eo_size.h = h;
		LOGW("resize (x:%d, y:%d, w:%d, h:%d)", x, y, w, h);
		ret = _mm_evas_renderer_apply_geometry(evas_info);
		if (ret != MM_ERROR_NONE)
			LOGW("fail to apply geometry info");
	}
	LOGD("[LEAVE]");
}

static void _evas_render_pre_cb(void *data, Evas *e, void *event_info)
{
	mm_evas_info *evas_info = data;
	if (!evas_info || !evas_info->eo) {
		LOGW("there is no esink info.... esink : %p, or eo is NULL returning", evas_info);
		return;
	}
	//LOGI("- test -"); //@@@@@ pre_cb will be deleted. actually it is useless
}

static void _evas_del_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	mm_evas_info *evas_info = data;
	LOGD("[ENTER]");
	if (!evas_info || !evas_info->eo) {
		return;
	}
	if (evas_info->eo) {
		_mm_evas_renderer_unset_callback(evas_info);
		evas_object_image_data_set(evas_info->eo, NULL);
		evas_info->eo = NULL;
	}
	LOGD("[LEAVE]");
}

void _evas_pipe_cb(void *data, void *buffer, update_info info)
{
	mm_evas_info *evas_info = data;
	int ret;
	LOGD("[ENTER]");

	if (!evas_info) {
		LOGW("evas_info is NULL", evas_info);
		return;
	}

	g_mutex_lock(&evas_info->mp_lock);

	if (!evas_info->eo) {
		LOGW("evas_info %p", evas_info);
		goto ERROR;
	}

	LOGD("evas_info : %p, evas_info->eo : %p", evas_info, evas_info->eo);
	if (info == UPDATE_VISIBILITY) {
		if (evas_info->visible == VISIBLE_FALSE) {
			evas_object_hide(evas_info->eo);
			LOGI("object hide..");
		} else {
			evas_object_show(evas_info->eo);
			LOGI("object show.. %d", evas_info->visible);
		}
		LOGD("[LEAVE]");
		g_mutex_unlock(&evas_info->mp_lock);
		return;
	}

	if (info != UPDATE_TBM_SURF) {
		LOGW("invalid info type : %d", info);
		goto ERROR;
	}

	/* normally get tbm surf in pipe callback. if not, we can get changed tbm surf by other threads */
	ret = media_packet_get_tbm_surface(evas_info->pkt[evas_info->cur_idx], &evas_info->tbm_surf);
	if (ret != MEDIA_PACKET_ERROR_NONE || !evas_info->tbm_surf) {
		LOGW("get_tbm_surface is failed");
		goto ERROR;
	}
	tbm_format tbm_fmt = tbm_surface_get_format(evas_info->tbm_surf);
	switch (tbm_fmt) {
	case TBM_FORMAT_NV12:
		LOGD("tbm_surface format : TBM_FORMAT_NV12");
		break;
	case TBM_FORMAT_YUV420:
		LOGD("tbm_surface format : TBM_FORMAT_YUV420");
		break;
	default:
		LOGW("tbm_surface format : unknown %d", tbm_fmt);
		break;
	}
	/* it is needed to skip setting when state is pause */
	LOGD("received (tbm_surf %p)", evas_info->tbm_surf);

	Evas_Native_Surface surf;
	surf.type = EVAS_NATIVE_SURFACE_TBM;
	surf.version = EVAS_NATIVE_SURFACE_VERSION;
	surf.data.tbm.buffer = evas_info->tbm_surf;
//  surf.data.tbm.rot = evas_info->rotate_angle;
//  surf.data.tbm.flip = evas_info->flip;

	rect_info result = { 0 };

	evas_object_geometry_get(evas_info->eo, &evas_info->eo_size.x, &evas_info->eo_size.y, &evas_info->eo_size.w, &evas_info->eo_size.h);
	if (!evas_info->eo_size.w || !evas_info->eo_size.h) {
		LOGE("there is no information for evas object size");
		goto ERROR;
	}
	_mm_evas_renderer_update_geometry(evas_info, &result);
	if (!result.w || !result.h) {
		LOGE("no information about geometry (%d, %d)", result.w, result.h);
		goto ERROR;
	}

	if (evas_info->use_ratio) {
//      surf.data.tbm.ratio = (float) evas_info->w / evas_info->h;
		LOGD("set ratio for letter mode");
	}
	evas_object_size_hint_align_set(evas_info->eo, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_size_hint_weight_set(evas_info->eo, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	if (evas_info->w > 0 && evas_info->h > 0)
		evas_object_image_size_set(evas_info->eo, evas_info->w, evas_info->h);

	evas_object_image_native_surface_set(evas_info->eo, &surf);
	LOGD("native surface set finish");

	if (result.x || result.y)
		LOGD("coordinate x, y (%d, %d) for locating video to center", result.x, result.y);
	evas_object_image_fill_set(evas_info->eo, result.x, result.y, result.w, result.h);

	evas_object_image_pixels_dirty_set(evas_info->eo, EINA_TRUE);
	LOGD("GEO_METHOD : src(%dx%d), dst(%dx%d), dst_x(%d), dst_y(%d), rotate(%d), flip(%d)", evas_info->w, evas_info->h, evas_info->eo_size.w, evas_info->eo_size.h, evas_info->eo_size.x, evas_info->eo_size.y, evas_info->rotate_angle, evas_info->flip);

	/* when _evas_pipe_cb is called sequentially, previous packet and current packet will be the same */
	if ((evas_info->prev_idx != -1) && evas_info->pkt[evas_info->prev_idx] && (evas_info->prev_idx != evas_info->cur_idx)) {
		LOGD("destroy previous packet [%p] idx %d", evas_info->pkt[evas_info->prev_idx], evas_info->prev_idx);
		if (media_packet_destroy(evas_info->pkt[evas_info->prev_idx]) != MEDIA_PACKET_ERROR_NONE)
			LOGE("media_packet_destroy failed %p", evas_info->pkt[evas_info->prev_idx]);
		evas_info->pkt[evas_info->prev_idx] = NULL;
		evas_info->sent_buffer_cnt--;
		LOGD("sent packet %d", evas_info->sent_buffer_cnt);
	}
	evas_info->prev_idx = evas_info->cur_idx;

	LOGD("[LEAVE]");
	g_mutex_unlock(&evas_info->mp_lock);

	return;

 ERROR:
	if ((evas_info->prev_idx != -1) && evas_info->pkt[evas_info->prev_idx]) {
		LOGD("cant render. destroy previous packet [%p] idx %d", evas_info->pkt[evas_info->prev_idx], evas_info->prev_idx);
		if (media_packet_destroy(evas_info->pkt[evas_info->prev_idx]) != MEDIA_PACKET_ERROR_NONE)
			LOGE("media_packet_destroy failed %p", evas_info->pkt[evas_info->prev_idx]);
		evas_info->pkt[evas_info->prev_idx] = NULL;
		evas_info->sent_buffer_cnt--;
	}
	evas_info->prev_idx = evas_info->cur_idx;
	g_mutex_unlock(&evas_info->mp_lock);
}

static int _get_video_size(media_packet_h packet, mm_evas_info *evas_info)
{
	media_format_h fmt;
	if (media_packet_get_format(packet, &fmt) == MEDIA_PACKET_ERROR_NONE) {
		int w, h;
		if (media_format_get_video_info(fmt, NULL, &w, &h, NULL, NULL) == MEDIA_PACKET_ERROR_NONE) {
			LOGD("video width = %d, height =%d", w, h);
			evas_info->w = w;
			evas_info->h = h;
			return true;
		} else
			LOGW("media_format_get_video_info is failed");
		if (media_format_unref(fmt) != MEDIA_PACKET_ERROR_NONE)	//because of media_packet_get_format
			LOGW("media_format_unref is failed");
	} else {
		LOGW("media_packet_get_format is failed");
	}
	return false;
}

int _find_empty_index(mm_evas_info *evas_info)
{
	int i;
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (!evas_info->pkt[i]) {
			LOGD("selected idx %d", i);
			return i;
		}
	}
	LOGE("there is no empty idx");

	return -1;
}

#if 0
void _reset_pipe(mm_evas_info *evas_info)
{
	int i = 0;
	int ret = MEDIA_PACKET_ERROR_NONE;

	/* delete old pipe */
	if (evas_info->epipe) {
		LOGD("pipe %p will be deleted", evas_info->epipe);
		ecore_pipe_del(evas_info->epipe);
		evas_info->epipe = NULL;
	}

	/* destroy all packets */
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (evas_info->pkt[i]) {
			LOGD("destroy packet [%p]", evas_info->pkt[i]);
			ret = media_packet_destroy(evas_info->pkt[i]);
			if (ret != MEDIA_PACKET_ERROR_NONE)
				LOGW("media_packet_destroy failed %p", evas_info->pkt[i]);
			else
				evas_info->sent_buffer_cnt--;
			evas_info->pkt[i] = NULL;
		}
	}

	/* make new pipe */
	if (!evas_info->epipe) {
		evas_info->epipe = ecore_pipe_add((Ecore_Pipe_Cb) _evas_pipe_cb, evas_info);
		if (!evas_info->epipe) {
			LOGE("pipe is not created");
		}
		LOGD("created pipe %p", evas_info->epipe);
	}

	return;
}
#endif
static void _mm_evas_renderer_set_callback(mm_evas_info *evas_info)
{
	if (evas_info->eo) {
		SET_EVAS_OBJECT_EVENT_CALLBACK(evas_info->eo, evas_info);
		SET_EVAS_EVENT_CALLBACK(evas_object_evas_get(evas_info->eo), evas_info);
	}
}

static void _mm_evas_renderer_unset_callback(mm_evas_info *evas_info)
{
	if (evas_info->eo) {
		UNSET_EVAS_OBJECT_EVENT_CALLBACK(evas_info->eo);
		UNSET_EVAS_EVENT_CALLBACK(evas_object_evas_get(evas_info->eo));
	}
}

int _mm_evas_renderer_create(mm_evas_info **evas_info)
{
	mm_evas_info *ptr = NULL;
	ptr = g_malloc0(sizeof(mm_evas_info));

	if (!ptr) {
		LOGE("Cannot allocate memory for evas_info\n");
		goto ERROR;
	} else {
		*evas_info = ptr;
		LOGD("Success create evas_info(%p)", *evas_info);
	}
	g_mutex_init(&ptr->mp_lock);

	return MM_ERROR_NONE;

 ERROR:
	*evas_info = NULL;
	return MM_ERROR_OUT_OF_STORAGE;
}

int _mm_evas_renderer_destroy(mm_evas_info **evas_info)
{
	mm_evas_info *ptr = (mm_evas_info *)*evas_info;
	MM_CHECK_NULL(ptr);
	int ret = MM_ERROR_NONE;

	LOGD("finalize evas_info %p", ptr);

	ret = _mm_evas_renderer_reset(ptr);
	g_mutex_clear(&ptr->mp_lock);

	g_free(ptr);
	ptr = NULL;

	return ret;
}

int _mm_evas_renderer_set_info(mm_evas_info *evas_info, Evas_Object *eo)
{
	MM_CHECK_NULL(evas_info);
	MM_CHECK_NULL(eo);

	LOGD("set evas_info");

	evas_info->prev_idx = evas_info->cur_idx = -1;
	evas_info->eo = eo;
	evas_info->epipe = ecore_pipe_add((Ecore_Pipe_Cb) _evas_pipe_cb, evas_info);
	if (!evas_info->epipe) {
		LOGE("pipe is not created");
		return MM_ERROR_UNKNOWN;
	}
	LOGD("created pipe %p", evas_info->epipe);
	_mm_evas_renderer_set_callback(evas_info);

	evas_object_geometry_get(evas_info->eo, &evas_info->eo_size.x, &evas_info->eo_size.y, &evas_info->eo_size.w, &evas_info->eo_size.h);
	LOGI("evas object %p (%d, %d, %d, %d)", evas_info->eo, evas_info->eo_size.x, evas_info->eo_size.y, evas_info->eo_size.w, evas_info->eo_size.h);

	return MM_ERROR_NONE;
}

int _mm_evas_renderer_reset(mm_evas_info *evas_info)
{
	MM_CHECK_NULL(evas_info);

	int i;
	int ret = MEDIA_PACKET_ERROR_NONE;

	if (evas_info->eo) {
		_mm_evas_renderer_unset_callback(evas_info);
		evas_object_image_data_set(evas_info->eo, NULL);
		evas_info->eo = NULL;
	}
	if (evas_info->epipe) {
		LOGD("pipe %p will be deleted", evas_info->epipe);
		ecore_pipe_del(evas_info->epipe);
		evas_info->epipe = NULL;
	}

	evas_info->eo_size.x = evas_info->eo_size.y = evas_info->eo_size.w = evas_info->eo_size.h = 0;
	evas_info->w = evas_info->h = 0;
	evas_info->tbm_surf = NULL;

	g_mutex_lock(&evas_info->mp_lock);
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (evas_info->pkt[i]) {
			/* destroy all packets */
			LOGD("destroy packet [%p]", evas_info->pkt[i]);
			ret = media_packet_destroy(evas_info->pkt[i]);
			if (ret != MEDIA_PACKET_ERROR_NONE)
				LOGW("media_packet_destroy failed %p", evas_info->pkt[i]);
			else
				evas_info->sent_buffer_cnt--;
			evas_info->pkt[i] = NULL;
		}
	}
	g_mutex_unlock(&evas_info->mp_lock);

	if (evas_info->sent_buffer_cnt != 0)
		LOGE("it should be 0 --> [%d]", evas_info->sent_buffer_cnt);
	evas_info->sent_buffer_cnt = 0;
	evas_info->prev_idx = evas_info->cur_idx = -1;

	return MM_ERROR_NONE;
}

void _mm_evas_renderer_update_geometry(mm_evas_info *evas_info, rect_info *result)
{
	if (!evas_info || !evas_info->eo) {
		LOGW("there is no evas_info or evas object");
		return;
	}
	if (!result) {
		LOGW("there is no rect info");
		return;
	}

	result->x = 0;
	result->y = 0;

	switch (evas_info->display_geometry_method) {
	case DISP_GEO_METHOD_LETTER_BOX:
		/* set black padding for letter box mode */
		LOGD("letter box mode");
		evas_info->use_ratio = TRUE;
		result->w = evas_info->eo_size.w;
		result->h = evas_info->eo_size.h;
		break;
	case DISP_GEO_METHOD_ORIGIN_SIZE:
		LOGD("origin size mode");
		evas_info->use_ratio = FALSE;
		/* set coordinate for each case */
		result->x = (evas_info->eo_size.w - evas_info->w) / 2;
		result->y = (evas_info->eo_size.h - evas_info->h) / 2;
		result->w = evas_info->w;
		result->h = evas_info->h;
		break;
	case DISP_GEO_METHOD_FULL_SCREEN:
		LOGD("full screen mode");
		evas_info->use_ratio = FALSE;
		result->w = evas_info->eo_size.w;
		result->h = evas_info->eo_size.h;
		break;
	case DISP_GEO_METHOD_CROPPED_FULL_SCREEN:
		LOGD("cropped full screen mode");
		evas_info->use_ratio = FALSE;
		/* compare evas object's ratio with video's */
		if ((evas_info->eo_size.w / evas_info->eo_size.h) > (evas_info->w / evas_info->h)) {
			result->w = evas_info->eo_size.w;
			result->h = evas_info->eo_size.w * evas_info->h / evas_info->w;
			result->y = -(result->h - evas_info->eo_size.h) / 2;
		} else {
			result->w = evas_info->eo_size.h * evas_info->w / evas_info->h;
			result->h = evas_info->eo_size.h;
			result->x = -(result->w - evas_info->eo_size.w) / 2;
		}
		break;
	case DISP_GEO_METHOD_ORIGIN_SIZE_OR_LETTER_BOX:
		LOGD("origin size or letter box mode");
		/* if video size is smaller than evas object's, it will be set to origin size mode */
		if ((evas_info->eo_size.w > evas_info->w) && (evas_info->eo_size.h > evas_info->h)) {
			LOGD("origin size mode");
			evas_info->use_ratio = FALSE;
			/* set coordinate for each case */
			result->x = (evas_info->eo_size.w - evas_info->w) / 2;
			result->y = (evas_info->eo_size.h - evas_info->h) / 2;
			result->w = evas_info->w;
			result->h = evas_info->h;
		} else {
			LOGD("letter box mode");
			evas_info->use_ratio = TRUE;
			result->w = evas_info->eo_size.w;
			result->h = evas_info->eo_size.h;
		}
		break;
	default:
		LOGW("unsupported mode.");
		break;
	}
	LOGD("geometry result [%d, %d, %d, %d]", result->x, result->y, result->w, result->h);
}

int _mm_evas_renderer_apply_geometry(mm_evas_info *evas_info)
{
	if (!evas_info || !evas_info->eo) {
		LOGW("there is no evas_info or evas object");
		return MM_ERROR_NONE;
	}

	Evas_Native_Surface *surf = evas_object_image_native_surface_get(evas_info->eo);
	rect_info result = { 0 };

	if (surf) {
		LOGD("native surface exists");
//      surf->data.tbm.rot = evas_info->rotate_angle;
//      surf->data.tbm.flip = evas_info->flip;
		evas_object_image_native_surface_set(evas_info->eo, surf);

		_mm_evas_renderer_update_geometry(evas_info, &result);

		if (evas_info->use_ratio) {
//          surf->data.tbm.ratio = (float) evas_info->w / evas_info->h;
			LOGD("set ratio for letter mode");
		}

		if (result.x || result.y)
			LOGD("coordinate x, y (%d, %d) for locating video to center", result.x, result.y);

		evas_object_image_fill_set(evas_info->eo, result.x, result.y, result.w, result.h);
		return MM_ERROR_NONE;
	} else
		LOGW("there is no surf");
	/* FIXME: before pipe_cb is invoked, apply_geometry can be called. */
	return MM_ERROR_NONE;
}

void mm_evas_renderer_write(media_packet_h packet, void *data)
{
	if (!packet) {
		LOGE("packet %p is NULL", packet);
		return;
	}
	mm_evas_info *handle = (mm_evas_info *)data;
	int ret = MEDIA_PACKET_ERROR_NONE;
	bool has;

	LOGD("packet [%p]", packet);

	if (!data || !handle) {
		LOGE("handle %p or evas_info %p is NULL", data, handle);
		goto ERROR;
	}

	ret = media_packet_has_tbm_surface_buffer(packet, &has);
	if (ret != MEDIA_PACKET_ERROR_NONE) {
		LOGW("has_tbm_surface is failed");
		goto ERROR;
	}
	/* FIXME: when setCaps occurs, _get_video_size should be called */
	/* currently we are always checking it */
	if (has && _get_video_size(packet, handle)) {
		/* Attention! if this error occurs, we need to consider managing buffer */
		if (handle->sent_buffer_cnt > 14) {
			LOGE("too many buffers are not released %d", handle->sent_buffer_cnt);
			goto ERROR;
#if 0
			/* FIXME: fix this logic */
			/* destroy all media packets and reset pipe at present */
			/* Attention! it might free buffer that is being rendered */
			g_mutex_lock(&handle->mp_lock);
			_reset_pipe(handle);
			g_mutex_unlock(&handle->mp_lock);
#endif
		}
		handle->cur_idx = _find_empty_index(handle);
		if (handle->cur_idx == -1)
			goto ERROR;
		handle->pkt[handle->cur_idx] = packet;
		handle->sent_buffer_cnt++;
		LOGD("sent packet %d", handle->sent_buffer_cnt);
		ret = ecore_pipe_write(handle->epipe, handle, UPDATE_TBM_SURF);
		if (!ret) {
			handle->pkt[handle->cur_idx] = NULL;
			handle->cur_idx = handle->prev_idx;
			handle->sent_buffer_cnt--;
			LOGW("Failed to ecore_pipe_write() for updating tbm surf\n");
			goto ERROR;
		}
	} else {
		LOGW("no tbm_surf");
		goto ERROR;
	}

	return;

 ERROR:
	/* destroy media_packet immediately */
	if (packet) {
		g_mutex_lock(&handle->mp_lock);
		LOGD("cant write. destroy packet [%p]", packet);
		if (media_packet_destroy(packet) != MEDIA_PACKET_ERROR_NONE)
			LOGE("media_packet_destroy failed %p", packet);
		packet = NULL;
		g_mutex_unlock(&handle->mp_lock);
	}
	return;
}

int mm_evas_renderer_update_param(MMHandleType handle)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type.");
		return ret;
	}

	/* when handle is realized, we need to update all properties */
	if (evas_info) {
		LOGD("set video param : evas-object %x, method %d", evas_info->eo, evas_info->display_geometry_method);
		LOGD("set video param : visible %d", evas_info->visible);
		LOGD("set video param : rotate %d", evas_info->rotate_angle);

		ret = _mm_evas_renderer_apply_geometry(evas_info);
		if (ret != MM_ERROR_NONE)
			return ret;

		if (evas_info->epipe) {
			ret = ecore_pipe_write(evas_info->epipe, &evas_info->visible, UPDATE_VISIBILITY);
			if (!ret) {
				LOGW("fail to ecore_pipe_write() for updating visibility\n");
				ret = MM_ERROR_UNKNOWN;
			} else {
				ret = MM_ERROR_NONE;
			}
		}
	}

	return ret;
}

int mm_evas_renderer_create(MMHandleType *handle, Evas_Object *eo)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = NULL;

	ret = _mm_evas_renderer_create(&evas_info);
	if (ret != MM_ERROR_NONE) {
		LOGE("fail to create evas_info");
		return ret;
	}
	ret = _mm_evas_renderer_set_info(evas_info, eo);
	if (ret != MM_ERROR_NONE) {
		LOGE("fail to init evas_info");
		if (_mm_evas_renderer_destroy(&evas_info) != MM_ERROR_NONE)
			LOGE("fail to destroy evas_info");
		return ret;
	}

	*handle = (MMHandleType)evas_info;

	return MM_ERROR_NONE;
}

int mm_evas_renderer_destroy(MMHandleType *handle)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)*handle;

	if (!evas_info) {
		LOGD("skip it. it is not evas surface type.");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	ret = _mm_evas_renderer_destroy(&evas_info);
	if (ret != MM_ERROR_NONE) {
		LOGE("fail to destroy evas_info");
		return ret;
	}
	*handle = NULL;

	return MM_ERROR_NONE;
}

int mm_evas_renderer_set_visible(MMHandleType handle, bool visible)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	if (visible)
		evas_info->visible = VISIBLE_TRUE;
	else
		evas_info->visible = VISIBLE_FALSE;

	if (evas_info->epipe) {
		ret = ecore_pipe_write(evas_info->epipe, &visible, UPDATE_VISIBILITY);
		if (!ret) {
			LOGW("fail to ecore_pipe_write() for updating visibility\n");
			ret = MM_ERROR_UNKNOWN;
		} else {
			ret = MM_ERROR_NONE;
		}
	} else {
		LOGW("there is no epipe. we cant update it");
	}

	return ret;
}

int mm_evas_renderer_get_visible(MMHandleType handle, bool *visible)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	if (evas_info->visible == VISIBLE_FALSE)
		*visible = FALSE;
	else
		*visible = TRUE;

	return MM_ERROR_NONE;
}

int mm_evas_renderer_set_rotation(MMHandleType handle, int rotate)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	evas_info->rotate_angle = rotate;
	ret = _mm_evas_renderer_apply_geometry(evas_info);

	return ret;
}

int mm_evas_renderer_get_rotation(MMHandleType handle, int *rotate)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}
	*rotate = evas_info->rotate_angle;

	return MM_ERROR_NONE;
}

int mm_evas_renderer_set_geometry(MMHandleType handle, int mode)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	evas_info->display_geometry_method = mode;
	ret = _mm_evas_renderer_apply_geometry(evas_info);

	return ret;
}

int mm_evas_renderer_get_geometry(MMHandleType handle, int *mode)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}
	*mode = evas_info->display_geometry_method;

	return MM_ERROR_NONE;
}
