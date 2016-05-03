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
#include <sys/syscall.h>
#include "mm_evas_renderer.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "MM_EVAS_RENDER"
//#define _INTERNAL_DEBUG_ /* debug only */

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
	DISP_GEO_METHOD_CUSTOM_ROI,
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

#ifdef _INTERNAL_DEBUG_
void __print_idx(mm_evas_info *evas_info);
#endif
/* internal */
void _free_previous_packets(mm_evas_info *evas_info);
int _flush_packets(mm_evas_info *evas_info);
int _mm_evas_renderer_create(mm_evas_info **evas_info);
int _mm_evas_renderer_destroy(mm_evas_info **evas_info);
int _mm_evas_renderer_set_info(mm_evas_info *evas_info, Evas_Object *eo);
int _mm_evas_renderer_reset(mm_evas_info *evas_info);
void _mm_evas_renderer_update_geometry(mm_evas_info *evas_info, rect_info *result);
int _mm_evas_renderer_apply_geometry(mm_evas_info *evas_info);
int _mm_evas_renderer_retrieve_all_packets(mm_evas_info *evas_info, bool keep_screen);
int _mm_evas_renderer_make_flush_buffer(mm_evas_info *evas_info);
void _mm_evas_renderer_release_flush_buffer(mm_evas_info *evas_info);
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

	/* flush will be executed in this callback normally,
	because native_surface_set must be called in main thread */
	if (evas_info->retrieve_packet) {
		g_mutex_lock(&evas_info->idx_lock);
		if (_flush_packets(evas_info) != MM_ERROR_NONE)
			LOGE("flushing packets are failed");
		g_mutex_unlock(&evas_info->idx_lock);
	}
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

	LOGD("[ENTER]");

	if (!evas_info) {
		LOGW("evas_info is NULL", evas_info);
		return;
	}

	g_mutex_lock(&evas_info->mp_lock);

	if (!evas_info->eo) {
		LOGW("evas_info %p", evas_info);
		g_mutex_unlock(&evas_info->mp_lock);
		return;
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
		g_mutex_unlock(&evas_info->mp_lock);
		return;
	}

	if (evas_info->cur_idx==-1 || !evas_info->pkt_info[evas_info->cur_idx].tbm_surf) {
		LOGW("cur_idx %d, tbm_surf may be NULL", evas_info->cur_idx);
		g_mutex_unlock(&evas_info->mp_lock);
		return;
	}
	/* perhaps, it is needed to skip setting when state is pause */

	g_mutex_lock(&evas_info->idx_lock);
	/* index */
	gint cur_idx = evas_info->cur_idx;
	gint prev_idx = evas_info->pkt_info[cur_idx].prev;

	LOGD("received (idx %d, packet %p)", cur_idx, evas_info->pkt_info[cur_idx].packet);

	tbm_format tbm_fmt = tbm_surface_get_format(evas_info->pkt_info[cur_idx].tbm_surf);
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

	Evas_Native_Surface surf;
	surf.type = EVAS_NATIVE_SURFACE_TBM;
	surf.version = EVAS_NATIVE_SURFACE_VERSION;
	surf.data.tbm.buffer = evas_info->pkt_info[cur_idx].tbm_surf;
	surf.data.tbm.rot = evas_info->rotate_angle;
	surf.data.tbm.flip = evas_info->flip;

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
	if (evas_info->display_geometry_method == DISP_GEO_METHOD_CUSTOM_ROI) {
		surf.data.tbm.roi.use_roi = EINA_TRUE;
		surf.data.tbm.roi.x = evas_info->dst_roi.x;
		surf.data.tbm.roi.y = evas_info->dst_roi.y;
		surf.data.tbm.roi.w = evas_info->dst_roi.w;
		surf.data.tbm.roi.h = evas_info->dst_roi.h;
	} else if (evas_info->use_ratio) {
		surf.data.tbm.ratio = (float) evas_info->w / evas_info->h;
		LOGD("set ratio for letter mode");
	}

	if(evas_info->update_needed) {
		evas_object_image_native_surface_set(evas_info->eo, NULL);
		evas_info->update_needed = FALSE;
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
	if ((prev_idx != -1) && evas_info->pkt_info[prev_idx].packet && (prev_idx != cur_idx))
		_free_previous_packets(evas_info);

	LOGD("[LEAVE]");
	g_mutex_unlock(&evas_info->idx_lock);
	g_mutex_unlock(&evas_info->mp_lock);

	return;

 ERROR:
	if ((prev_idx != -1) && evas_info->pkt_info[prev_idx].packet) {
		LOGI("cant render");
		_free_previous_packets(evas_info);
	}
	g_mutex_unlock(&evas_info->idx_lock);
	g_mutex_unlock(&evas_info->mp_lock);
}

#ifdef _INTERNAL_DEBUG_
void __print_idx(mm_evas_info *evas_info)
{
	gint prev_idx = evas_info->pkt_info[evas_info->cur_idx].prev;
	LOGE("***** start cur_idx : %d -> prev_idx : %d", evas_info->cur_idx, prev_idx);
	while(prev_idx != -1)
	{
		LOGE("***** cur_idx : %d -> prev_idx : %d", prev_idx, evas_info->pkt_info[prev_idx].prev);
		prev_idx = evas_info->pkt_info[prev_idx].prev;
	}
	LOGE("***** end");
	return;
}
#endif

void _free_previous_packets(mm_evas_info *evas_info)
{
	gint index = evas_info->cur_idx;
	gint prev_idx = evas_info->pkt_info[index].prev;

	while(prev_idx != -1)
	{
		LOGD("destroy previous packet [%p] idx %d", evas_info->pkt_info[prev_idx].packet, prev_idx);
		if (media_packet_destroy(evas_info->pkt_info[prev_idx].packet) != MEDIA_PACKET_ERROR_NONE)
			LOGE("media_packet_destroy failed %p", evas_info->pkt_info[prev_idx].packet);
		evas_info->pkt_info[prev_idx].packet = NULL;
		evas_info->pkt_info[prev_idx].tbm_surf = NULL;
		evas_info->pkt_info[index].prev = -1;
		evas_info->sent_buffer_cnt--;

		/* move index to previous index */
		index= prev_idx;
		prev_idx = evas_info->pkt_info[prev_idx].prev;
		LOGD("sent packet %d", evas_info->sent_buffer_cnt);
	}
	return;
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
		if (media_format_unref(fmt) != MEDIA_PACKET_ERROR_NONE)	/* because of media_packet_get_format */
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
		if (!evas_info->pkt_info[i].packet) {
			LOGD("selected idx %d", i);
			return i;
		}
	}
	LOGE("there is no empty idx");

	return -1;
}

int _flush_packets(mm_evas_info *evas_info)
{
	int ret = MM_ERROR_NONE;
	int ret_mp = MEDIA_PACKET_ERROR_NONE;
	int i = 0;

	if (!evas_info) {
		LOGW("there is no esink info");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	/* update the screen only if visible is ture */
	if (evas_info->keep_screen && (evas_info->visible != VISIBLE_FALSE)) {
		Evas_Native_Surface surf;
		rect_info result = { 0 };
		evas_object_geometry_get(evas_info->eo, &evas_info->eo_size.x, &evas_info->eo_size.y, &evas_info->eo_size.w, &evas_info->eo_size.h);
		if (!evas_info->eo_size.w || !evas_info->eo_size.h) {
			LOGE("there is no information for evas object size");
			return MM_ERROR_INVALID_ARGUMENT;
		}
		_mm_evas_renderer_update_geometry(evas_info, &result);
		if (!result.w || !result.h) {
			LOGE("no information about geometry (%d, %d)", result.w, result.h);
			return MM_ERROR_INVALID_ARGUMENT;
		}

		if (evas_info->use_ratio) {
			surf.data.tbm.ratio = (float) evas_info->w / evas_info->h;
			LOGD("set ratio for letter mode");
		}
		evas_object_size_hint_align_set(evas_info->eo, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_size_hint_weight_set(evas_info->eo, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		if (evas_info->w > 0 && evas_info->h > 0)
			evas_object_image_size_set(evas_info->eo, evas_info->w, evas_info->h);

		if (result.x || result.y)
			LOGD("coordinate x, y (%d, %d) for locating video to center", result.x, result.y);
		evas_object_image_fill_set(evas_info->eo, result.x, result.y, result.w, result.h);

		/* set flush buffer */
		surf.type = EVAS_NATIVE_SURFACE_TBM;
		surf.version = EVAS_NATIVE_SURFACE_VERSION;
		surf.data.tbm.buffer = evas_info->flush_buffer->tbm_surf;
		surf.data.tbm.rot = evas_info->rotate_angle;
		surf.data.tbm.flip = evas_info->flip;
		if (evas_info->display_geometry_method == DISP_GEO_METHOD_CUSTOM_ROI) {
			surf.data.tbm.roi.use_roi=EINA_TRUE;
			surf.data.tbm.roi.x = evas_info->dst_roi.x;
			surf.data.tbm.roi.y = evas_info->dst_roi.y;
			surf.data.tbm.roi.w = evas_info->dst_roi.w;
			surf.data.tbm.roi.h = evas_info->dst_roi.h;
		}
		evas_object_image_native_surface_set(evas_info->eo, &surf);

		LOGD("flush_buffer surf(%p), rotate(%d), flip(%d)", evas_info->flush_buffer->tbm_surf, evas_info->rotate_angle, evas_info->flip);
	} else {
		/* unset evas native surface for displaying black screen */
		evas_object_image_native_surface_set (evas_info->eo, NULL);
		evas_object_image_data_set (evas_info->eo, NULL);
	}
	LOGD("sent packet %d", evas_info->sent_buffer_cnt);

	/* destroy all packets */
	g_mutex_lock(&evas_info->mp_lock);
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (evas_info->pkt_info[i].packet) {
			LOGD("destroy packet [%p]", evas_info->pkt_info[i].packet);
			ret_mp = media_packet_destroy(evas_info->pkt_info[i].packet);
			if (ret_mp != MEDIA_PACKET_ERROR_NONE) {
				LOGW("media_packet_destroy failed %p", evas_info->pkt_info[i].packet);
				ret = MM_ERROR_UNKNOWN;
			}
			else
				evas_info->sent_buffer_cnt--;
			evas_info->pkt_info[i].packet = NULL;
			evas_info->pkt_info[i].tbm_surf = NULL;
			evas_info->pkt_info[i].prev = -1;
		}
	}

	if (evas_info->sent_buffer_cnt != 0)
		LOGE("it should be 0 --> [%d]", evas_info->sent_buffer_cnt);
	evas_info->sent_buffer_cnt = 0;
	evas_info->cur_idx = -1;
	g_mutex_unlock(&evas_info->mp_lock);

	evas_object_image_pixels_dirty_set (evas_info->eo, EINA_TRUE);
	evas_info->retrieve_packet = FALSE;

	return ret;
}

#if 0
int _reset_pipe(mm_evas_info *evas_info)
{
	int i = 0;
	int ret = MM_ERROR_NONE;
	int ret_mp = MEDIA_PACKET_ERROR_NONE;

	/* delete old pipe */
	if (evas_info->epipe) {
		LOGD("pipe %p will be deleted", evas_info->epipe);
		ecore_pipe_del(evas_info->epipe);
		evas_info->epipe = NULL;
	}

	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (evas_info->pkt_info[i].packet) {
			/* destroy all packets */
			LOGD("destroy packet [%p]", evas_info->pkt_info[i].packet);
			ret_mp = media_packet_destroy(evas_info->pkt_info[i].packet);
			if (ret_mp != MEDIA_PACKET_ERROR_NONE) {
				LOGW("media_packet_destroy failed %p", evas_info->pkt_info[i].packet);
				ret = MM_ERROR_UNKNOWN;
			}
			else
				evas_info->sent_buffer_cnt--;
			evas_info->pkt_info[i].packet = NULL;
			evas_info->pkt_info[i].tbm_surf = NULL;
			evas_info->pkt_info[i].prev = -1;
		}
	}

	if (evas_info->sent_buffer_cnt != 0)
		LOGE("it should be 0 --> [%d]", evas_info->sent_buffer_cnt);
	evas_info->sent_buffer_cnt = 0;
	evas_info->cur_idx = -1;

	/* make new pipe */
	if (!evas_info->epipe) {
		evas_info->epipe = ecore_pipe_add((Ecore_Pipe_Cb) _evas_pipe_cb, evas_info);
		if (!evas_info->epipe) {
			LOGE("pipe is not created");
			ret = MM_ERROR_UNKNOWN;
		}
		LOGD("created pipe %p", evas_info->epipe);
	}

	return ret;
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
	g_mutex_init(&ptr->idx_lock);

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
	g_mutex_clear(&ptr->idx_lock);

	g_free(ptr);
	ptr = NULL;

	return ret;
}

int _mm_evas_renderer_set_info(mm_evas_info *evas_info, Evas_Object *eo)
{
	MM_CHECK_NULL(evas_info);
	MM_CHECK_NULL(eo);
	g_mutex_lock(&evas_info->idx_lock);

	LOGD("set evas_info");
	int i;
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		evas_info->pkt_info[i].packet = NULL;
		evas_info->pkt_info[i].tbm_surf = NULL;
		evas_info->pkt_info[i].prev = -1;
	}
	evas_info->cur_idx = -1;
	evas_info->dst_roi.x = evas_info->dst_roi.y = evas_info->dst_roi.w = evas_info->dst_roi.h = 0;
	evas_info->eo = eo;
	evas_info->epipe = ecore_pipe_add((Ecore_Pipe_Cb) _evas_pipe_cb, evas_info);
	if (!evas_info->epipe) {
		LOGE("pipe is not created");
		g_mutex_unlock(&evas_info->idx_lock);
		return MM_ERROR_UNKNOWN;
	}
	LOGD("created pipe %p", evas_info->epipe);
	_mm_evas_renderer_set_callback(evas_info);

	evas_object_geometry_get(evas_info->eo, &evas_info->eo_size.x, &evas_info->eo_size.y, &evas_info->eo_size.w, &evas_info->eo_size.h);
	LOGI("evas object %p (%d, %d, %d, %d)", evas_info->eo, evas_info->eo_size.x, evas_info->eo_size.y, evas_info->eo_size.w, evas_info->eo_size.h);

	g_mutex_unlock(&evas_info->idx_lock);

	return MM_ERROR_NONE;
}

int _mm_evas_renderer_reset(mm_evas_info *evas_info)
{
	MM_CHECK_NULL(evas_info);
	g_mutex_lock(&evas_info->idx_lock);

	int i;
	int ret = MM_ERROR_NONE;
	int ret_mp = MEDIA_PACKET_ERROR_NONE;

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
	evas_info->dst_roi.x = evas_info->dst_roi.y = evas_info->dst_roi.w = evas_info->dst_roi.h = 0;
	evas_info->w = evas_info->h = 0;

	if (evas_info->flush_buffer)
		_mm_evas_renderer_release_flush_buffer(evas_info);

	g_mutex_lock(&evas_info->mp_lock);
	for (i = 0; i < MAX_PACKET_NUM; i++) {
		if (evas_info->pkt_info[i].packet) {
			/* destroy all packets */
			LOGD("destroy packet [%p]", evas_info->pkt_info[i].packet);
			ret_mp = media_packet_destroy(evas_info->pkt_info[i].packet);
			if (ret_mp != MEDIA_PACKET_ERROR_NONE) {
				LOGW("media_packet_destroy failed %p", evas_info->pkt_info[i].packet);
				ret = MM_ERROR_UNKNOWN;
			}
			else
				evas_info->sent_buffer_cnt--;
			evas_info->pkt_info[i].packet = NULL;
			evas_info->pkt_info[i].tbm_surf = NULL;
			evas_info->pkt_info[i].prev = -1;
		}
	}
	g_mutex_unlock(&evas_info->mp_lock);
	if (evas_info->sent_buffer_cnt != 0)
		LOGE("it should be 0 --> [%d]", evas_info->sent_buffer_cnt);
	evas_info->sent_buffer_cnt = 0;
	evas_info->cur_idx = -1;

	g_mutex_unlock(&evas_info->idx_lock);

	return ret;
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
	case DISP_GEO_METHOD_CUSTOM_ROI:
		LOGD("custom roi mode");
		evas_info->use_ratio= FALSE;
		result->x = evas_info->dst_roi.x;
		result->y = evas_info->dst_roi.y;
		result->w = evas_info->dst_roi.w;
		result->h = evas_info->dst_roi.h;
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
		surf->data.tbm.rot = evas_info->rotate_angle;
		surf->data.tbm.flip = evas_info->flip;
		if (evas_info->display_geometry_method == DISP_GEO_METHOD_CUSTOM_ROI) {
			surf->data.tbm.roi.use_roi = EINA_TRUE;
			surf->data.tbm.roi.x = evas_info->dst_roi.x;
			surf->data.tbm.roi.y = evas_info->dst_roi.y;
			surf->data.tbm.roi.w = evas_info->dst_roi.w;
			surf->data.tbm.roi.h = evas_info->dst_roi.h;
		} else if (evas_info->use_ratio) {
			surf->data.tbm.ratio = (float) evas_info->w / evas_info->h;
			LOGD("set ratio for letter mode");
		}
		evas_object_image_native_surface_set(evas_info->eo, surf);

		_mm_evas_renderer_update_geometry(evas_info, &result);

		if (result.x || result.y)
			LOGD("coordinate x, y (%d, %d) for locating video to center", result.x, result.y);
		evas_object_image_fill_set(evas_info->eo, result.x, result.y, result.w, result.h);

		return MM_ERROR_NONE;
	} else
		LOGW("there is no surf");
	/* FIXME: before pipe_cb is invoked, apply_geometry can be called. */
	return MM_ERROR_NONE;
}

int _mm_evas_renderer_retrieve_all_packets(mm_evas_info *evas_info, bool keep_screen)
{
	MM_CHECK_NULL(evas_info);

	int ret = MM_ERROR_NONE;
	pid_t pid = getpid();
	pid_t tid = syscall(SYS_gettid);

	/* write and this API can be called at the same time.
	so lock is needed for counting sent_buffer_cnt correctly */
	g_mutex_lock(&evas_info->idx_lock);

	/* make flush buffer */
	if (keep_screen)
		ret = _mm_evas_renderer_make_flush_buffer(evas_info);
	evas_info->keep_screen = keep_screen;

	LOGD("pid [%d], tid [%d]", pid, tid);
	if (pid == tid) {
		/* in this case, we deem it is main thread */
		if (_flush_packets(evas_info) != MM_ERROR_NONE) {
			LOGE("flushing packets are failed");
			ret = MM_ERROR_UNKNOWN;
		}
	} else {
		/* it will be executed to write flush buffer and destroy media packets in pre_cb */
		evas_info->retrieve_packet = TRUE;
	}
	g_mutex_unlock(&evas_info->idx_lock);

	return ret;
}

/* make buffer for copying */
int _mm_evas_renderer_make_flush_buffer (mm_evas_info *evas_info)
{
	if (evas_info->cur_idx == -1) {
		LOGW("there is no remained buffer");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	media_packet_h packet = evas_info->pkt_info[evas_info->cur_idx].packet;
	MM_CHECK_NULL(packet);

	flush_info *flush_buffer = NULL;
	tbm_bo src_bo = NULL;
	tbm_surface_h src_tbm_surf = NULL;
	int src_size = 0;
	int size = 0;
	tbm_bo bo = NULL;
	tbm_format tbm_fmt;
	tbm_bo_handle vaddr_src = {0};
	tbm_bo_handle vaddr_dst = {0};
	int ret = MM_ERROR_NONE;

	if (evas_info->flush_buffer)
		_mm_evas_renderer_release_flush_buffer(evas_info);

	/* malloc buffer */
	flush_buffer = (flush_info *)malloc(sizeof(flush_info));
	if (flush_buffer == NULL) {
		LOGE("malloc is failed");
		return FALSE;
	}
	memset(flush_buffer, 0x0, sizeof(flush_info));

	ret = media_packet_get_tbm_surface(packet, &src_tbm_surf);
	if (ret != MEDIA_PACKET_ERROR_NONE || !src_tbm_surf) {
		LOGW("get_tbm_surface is failed");
		goto ERROR;
	}

	/* get src buffer info */
	tbm_fmt = tbm_surface_get_format(src_tbm_surf);
	src_bo = tbm_surface_internal_get_bo(src_tbm_surf, 0);
	src_size = tbm_bo_size(src_bo);
	if (!src_bo || !src_size) {
		LOGE("bo(%p), size(%d)", src_bo, src_size);
		goto ERROR;
	}

	/* create tbm surface */
	flush_buffer->tbm_surf = tbm_surface_create(evas_info->w, evas_info->h, tbm_fmt);
	if (!flush_buffer->tbm_surf)
	{
		LOGE("tbm_surf is NULL!!");
		goto ERROR;
	}

	/* get bo and size */
	bo = tbm_surface_internal_get_bo(flush_buffer->tbm_surf, 0);
	size = tbm_bo_size(bo);
	if (!bo || !size)
	{
		LOGE("bo(%p), size(%d)", bo, size);
		goto ERROR;
	}
	flush_buffer->bo = bo;

	vaddr_src = tbm_bo_map(src_bo, TBM_DEVICE_CPU, TBM_OPTION_READ|TBM_OPTION_WRITE);
	vaddr_dst = tbm_bo_map(bo, TBM_DEVICE_CPU, TBM_OPTION_READ|TBM_OPTION_WRITE);
	if (!vaddr_src.ptr || !vaddr_dst.ptr) {
		LOGW("get vaddr failed src %p, dst %p", vaddr_src.ptr, vaddr_dst.ptr);
		if (vaddr_src.ptr) {
			tbm_bo_unmap(src_bo);
		}
		if (vaddr_dst.ptr) {
			tbm_bo_unmap(bo);
		}
		goto ERROR;
	} else {
		memset (vaddr_dst.ptr, 0x0, size);
		LOGW ("tbm_bo_map(vaddr) is finished, bo(%p), vaddr(%p)", bo, vaddr_dst.ptr);
	}

	/* copy buffer */
	memcpy(vaddr_dst.ptr, vaddr_src.ptr, src_size);

	tbm_bo_unmap(src_bo);
	tbm_bo_unmap(bo);
	LOGW("copy is done. tbm surface : %p src_size : %d", flush_buffer->tbm_surf, src_size);

	evas_info->flush_buffer = flush_buffer;

	return MM_ERROR_NONE;

ERROR:
	if (flush_buffer) {
		if(flush_buffer->tbm_surf)
		{
			tbm_surface_destroy(flush_buffer->tbm_surf);
			flush_buffer->tbm_surf = NULL;
		}

		free(flush_buffer);
		flush_buffer = NULL;
	}
	return MM_ERROR_UNKNOWN;
}

/* release flush buffer */
void _mm_evas_renderer_release_flush_buffer (mm_evas_info *evas_info)
{
	LOGW("release FLUSH BUFFER start");
	if (evas_info->flush_buffer->bo) {
		evas_info->flush_buffer->bo = NULL;
	}
	if (evas_info->flush_buffer->tbm_surf) {
		tbm_surface_destroy(evas_info->flush_buffer->tbm_surf);
		evas_info->flush_buffer->tbm_surf = NULL;
	}

	LOGW("release FLUSH BUFFER done");

	free(evas_info->flush_buffer);
	evas_info->flush_buffer = NULL;

	return;
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
	tbm_surface_h tbm_surf;
	gint index;

	LOGD("packet [%p]", packet);

	if (!data || !handle) {
		LOGE("handle %p or evas_info %p is NULL", data, handle);
		goto INVALID_PARAM;
	}
	g_mutex_lock(&handle->idx_lock);

	ret = media_packet_has_tbm_surface_buffer(packet, &has);
	if (ret != MEDIA_PACKET_ERROR_NONE) {
		LOGW("has_tbm_surface is failed");
		goto ERROR;
	}
	/* FIXME: when setCaps occurs, _get_video_size should be called */
	/* currently we are always checking it */
	if (has && _get_video_size(packet, handle)) {
		/* Attention! if this error occurs, we need to consider managing buffer */
		if (handle->sent_buffer_cnt > 3) {
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
		ret = media_packet_get_tbm_surface(packet, &tbm_surf);
		if (ret != MEDIA_PACKET_ERROR_NONE || !tbm_surf) {
			LOGW("get_tbm_surface is failed");
			goto ERROR;
		}

		/* find new index for current packet */
		index = _find_empty_index(handle);
		if (index == -1) {
			goto ERROR;
		}

		/* save previous index */
		handle->pkt_info[index].prev = handle->cur_idx;
		handle->pkt_info[index].packet = packet;
		handle->pkt_info[index].tbm_surf = tbm_surf;
		handle->cur_idx = index;
		handle->sent_buffer_cnt++;
		LOGD("sent packet %d", handle->sent_buffer_cnt);

		ret = ecore_pipe_write(handle->epipe, handle, UPDATE_TBM_SURF);
		if (!ret) {
			handle->pkt_info[index].packet = NULL;
			handle->pkt_info[index].tbm_surf = NULL;
			handle->pkt_info[index].prev = -1;
			handle->cur_idx = handle->pkt_info[index].prev;
			handle->sent_buffer_cnt--;
			LOGW("Failed to ecore_pipe_write() for updating tbm surf\n");
			goto ERROR;
		}
	} else {
		LOGW("no tbm_surf");
		goto ERROR;
	}
	g_mutex_unlock(&handle->idx_lock);

	return;
ERROR:
	g_mutex_unlock(&handle->idx_lock);
INVALID_PARAM:
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
				return MM_ERROR_UNKNOWN;
			}
			evas_info->update_needed = TRUE;
			/* FIXME: pause state only */
			g_mutex_lock(&evas_info->idx_lock);
			ret = ecore_pipe_write(evas_info->epipe, evas_info, UPDATE_TBM_SURF);
			if (!ret) {
				LOGW("fail to ecore_pipe_write() for updating visibility\n");
				ret = MM_ERROR_UNKNOWN;
			} else {
				ret = MM_ERROR_NONE;
			}
			g_mutex_unlock(&evas_info->idx_lock);
		}
	}

	return ret;
}

int mm_evas_renderer_create(MMHandleType *handle, Evas_Object *eo)
{
	MM_CHECK_NULL(handle);

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
	MM_CHECK_NULL(handle);

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
	guint value;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	switch(rotate) {
	case DEGREE_0:
		value = EVAS_IMAGE_ORIENT_0;
		break;
	case DEGREE_90:
		value = EVAS_IMAGE_ORIENT_90;
		break;
	case DEGREE_180:
		value = EVAS_IMAGE_ORIENT_180;
		break;
	case DEGREE_270:
		value = EVAS_IMAGE_ORIENT_270;
		break;
	default:
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (evas_info->rotate_angle != value)
		evas_info->update_needed = TRUE;
	evas_info->rotate_angle = value;

	/* FIXME: pause state only */
	if (evas_info->epipe) {
		g_mutex_lock(&evas_info->idx_lock);
		ret = ecore_pipe_write(evas_info->epipe, evas_info, UPDATE_TBM_SURF);
		if (!ret) {
			LOGW("fail to ecore_pipe_write() for updating visibility\n");
			ret = MM_ERROR_UNKNOWN;
		} else {
			ret = MM_ERROR_NONE;
		}
		g_mutex_unlock(&evas_info->idx_lock);
	}

	return ret;
}

int mm_evas_renderer_get_rotation(MMHandleType handle, int *rotate)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	switch(evas_info->rotate_angle) {
	case EVAS_IMAGE_ORIENT_0:
		*rotate = DEGREE_0;
		break;
	case EVAS_IMAGE_ORIENT_90:
		*rotate = DEGREE_90;
		break;
	case EVAS_IMAGE_ORIENT_180:
		*rotate = DEGREE_180;
		break;
	case EVAS_IMAGE_ORIENT_270:
		*rotate = DEGREE_270;
		break;
	default:
		return MM_ERROR_INVALID_ARGUMENT;
	}

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
	if (evas_info->display_geometry_method != mode)
		evas_info->update_needed = TRUE;
	evas_info->display_geometry_method = mode;

	/* ecore_pipe_write is needed, because of setting ratio for letterbox mode */
	/* FIXME: pause state only */
	if (evas_info->epipe) {
		g_mutex_lock(&evas_info->idx_lock);
		ret = ecore_pipe_write(evas_info->epipe, evas_info, UPDATE_TBM_SURF);
		if (!ret) {
			LOGW("fail to ecore_pipe_write() for updating visibility\n");
			ret = MM_ERROR_UNKNOWN;
		} else {
			ret = MM_ERROR_NONE;
		}
		g_mutex_unlock(&evas_info->idx_lock);
	}

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

int mm_evas_renderer_set_roi_area(MMHandleType handle, int x, int y, int w, int h)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}
	if (!w || !h) {
		LOGW("invalid resolution");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	/* display mode is set to DISP_GEO_METHOD_CUSTOM_ROI internally */
	evas_info->display_geometry_method = DISP_GEO_METHOD_CUSTOM_ROI;
	evas_info->dst_roi.x = x;
	evas_info->dst_roi.y = y;
	evas_info->dst_roi.w = w;
	evas_info->dst_roi.h = h;
	evas_info->update_needed = TRUE;

	/* @@@ pipe_write could be needed because ratio can be changed on pause state */
	ret = _mm_evas_renderer_apply_geometry(evas_info);

	return ret;
}

int mm_evas_renderer_get_roi_area(MMHandleType handle, int *x, int *y, int *w, int *h)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}
	if (evas_info->display_geometry_method != DISP_GEO_METHOD_CUSTOM_ROI) {
		LOGW("invalid mode");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	*x = evas_info->dst_roi.x;
	*y = evas_info->dst_roi.y;
	*w = evas_info->dst_roi.w;
	*h = evas_info->dst_roi.h;

	return MM_ERROR_NONE;
}

int mm_evas_renderer_set_flip(MMHandleType handle, int flip)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info *)handle;
	guint value;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	switch(flip) {
	case FLIP_NONE :
		value = EVAS_IMAGE_ORIENT_NONE;
		break;
	case FLIP_HORIZONTAL:
		value = EVAS_IMAGE_FLIP_HORIZONTAL;
		break;
	case FLIP_VERTICAL:
		value = EVAS_IMAGE_FLIP_VERTICAL;
		break;
	case FLIP_BOTH:
		value = EVAS_IMAGE_ORIENT_180;
		break;
	default:
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (evas_info->flip != value)
		evas_info->update_needed = TRUE;
	evas_info->flip = value;

	/* FIXME: pause state only */
	if (evas_info->epipe) {
		g_mutex_lock(&evas_info->idx_lock);
		ret = ecore_pipe_write(evas_info->epipe, evas_info, UPDATE_TBM_SURF);
		if (!ret) {
			LOGW("fail to ecore_pipe_write() for updating visibility\n");
			ret = MM_ERROR_UNKNOWN;
		} else {
			ret = MM_ERROR_NONE;
		}
		g_mutex_unlock(&evas_info->idx_lock);
	}

	return ret;
}

int mm_evas_renderer_get_flip(MMHandleType handle, int *flip)
{
	mm_evas_info *evas_info = (mm_evas_info *)handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or handle is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}

	switch(evas_info->flip) {
	case EVAS_IMAGE_ORIENT_NONE:
		*flip = FLIP_NONE;
		break;
	case EVAS_IMAGE_FLIP_HORIZONTAL:
		*flip = FLIP_HORIZONTAL;
		break;
	case EVAS_IMAGE_FLIP_VERTICAL:
		*flip = FLIP_VERTICAL;
		break;
	case EVAS_IMAGE_ORIENT_180:
		*flip = FLIP_BOTH;
		break;
	default:
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return MM_ERROR_NONE;
}

int mm_evas_renderer_retrieve_all_packets (MMHandleType handle, bool keep_screen)
{
	int ret = MM_ERROR_NONE;
	mm_evas_info *evas_info = (mm_evas_info*) handle;

	if (!evas_info) {
		LOGW("skip it. it is not evas surface type or player is not prepared");
		return MM_ERROR_RESOURCE_NOT_INITIALIZED;
	}
	ret = _mm_evas_renderer_retrieve_all_packets(evas_info, keep_screen);

	return ret;
}
