// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: nosem46 <214189557+nosem46@users.noreply.github.com>
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/bitfield.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "nosem_cursor.h"
#include "meson_registers.h"
#include "meson_viu.h"
#include "meson_venc.h"

#include "nosem.h"
struct nosem_cursor {
	struct drm_plane base;
	struct meson_drm *priv;
	bool enabled;
	bool vsync_enabled;
};
#define to_nosem_cursor(x) container_of(x, struct nosem_cursor, base)

static int nosem_cursor_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;

	if (!new_plane_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	/*
	 * Only allow :
	 * - Upscaling up to 5x, vertical and horizontal
	 * - Final coordinates must match crtc size
	 */
	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   true, true);
}

static int nosem_cursor_atomic_async_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);

	if (!new_plane_state->crtc)
		return -EINVAL;
	if (plane != new_plane_state->crtc->cursor)
		return -EINVAL;
	if (!plane->state)
		return -EINVAL;
	if (!plane->state->fb)
		return -EINVAL;

	return nosem_cursor_atomic_check(plane, state);
}

/* Takes a fixed 16.16 number and converts it to integer. */
static inline int64_t fixed16_to_int(int64_t value)
{
	return value >> 16;
}

static void nosem_cursor_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct nosem_cursor *nosem_cursor = to_nosem_cursor(plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct drm_rect dest = drm_plane_state_dest(new_state);
	struct meson_drm *priv = nosem_cursor->priv;
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_gem_cma_object *gem;
	unsigned long flags;
	u8 canvas_id_osd2;

	uint32_t wrap = MESON_CANVAS_WRAP_NONE;
	/*
	 * Update Coordinates
	 * Update Formats
	 * Update Buffer
	 * Enable Plane
	 */
	spin_lock_irqsave(&priv->reg_lock, flags);
	/* Enable OSD and BLK0, set max global alpha */
	priv->viu.osd2_ctrl_stat = OSD_ENABLE |
				   (0x100 << OSD_GLOBAL_ALPHA_SHIFT) |
				   OSD_BLK0_ENABLE;

	priv->viu.osd2_ctrl_stat2 = readl(priv->io_base +
					  _REG(VIU_OSD2_CTRL_STAT2));

	canvas_id_osd2 = priv->canvas_id_osd2;

	/* Set up BLK0 to point to the right canvas */
	priv->viu.osd2_blk0_cfg[0] = canvas_id_osd2 << OSD_CANVAS_SEL;

	priv->viu.osd2_blk0_cfg[0] |= OSD_ENDIANNESS_LE;

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		priv->viu.osd2_blk0_cfg[0] |= OSD_BLK_MODE_32 |
					OSD_COLOR_MATRIX_32_ARGB;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		priv->viu.osd2_blk0_cfg[0] |= OSD_BLK_MODE_32 |
					OSD_COLOR_MATRIX_32_ABGR;
		break;
	case DRM_FORMAT_RGB888:
		priv->viu.osd2_blk0_cfg[0] |= OSD_BLK_MODE_24 |
					OSD_COLOR_MATRIX_24_RGB;
		break;
	case DRM_FORMAT_RGB565:
		priv->viu.osd2_blk0_cfg[0] |= OSD_BLK_MODE_16 |
					OSD_COLOR_MATRIX_16_RGB565;
		break;
	
	}

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		/* For XRGB, replace the pixel's alpha by 0xFF */
		priv->viu.osd2_ctrl_stat2 |= OSD_REPLACE_EN;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		/* For ARGB, use the pixel's alpha */
		priv->viu.osd2_ctrl_stat2 &= ~OSD_REPLACE_EN;
		break;
	}

	/*
	 * The format of these registers is (x2 << 16 | x1),
	 * where x2 is exclusive.
	 * e.g. +30x1920 would be (1919 << 16) | 30
	 */
	priv->viu.osd2_blk0_cfg[1] =
				((fixed16_to_int(new_state->src.x2) - 1) << 16) |
				fixed16_to_int(new_state->src.x1);
	priv->viu.osd2_blk0_cfg[2] =
				((fixed16_to_int(new_state->src.y2) - 1) << 16) |
				fixed16_to_int(new_state->src.y1);
	priv->viu.osd2_blk0_cfg[3] = ((dest.x2 - 1) << 16) | dest.x1;
	priv->viu.osd2_blk0_cfg[4] = ((dest.y2 - 1) << 16) | dest.y1;

	priv->viu.osd_blend_din1_scope_h = ((dest.x2 - 1) << 16) | dest.x1;
	priv->viu.osd_blend_din1_scope_v = ((dest.y2 - 1) << 16) | dest.y1;

	priv->viu.osd2_upd |= UPDATE_CFG;
	/* Update Canvas with buffer address */
	gem = drm_fb_cma_get_gem_obj(fb, 0);
	
	if(priv->viu.osd2_reset || priv->viu.osd2_addr != gem->paddr || priv->viu.osd2_stride != fb->pitches[0] 
		|| priv->viu.osd2_height != fb->height || priv->viu.osd2_width != fb->width || priv->viu.osd2_wrap != wrap){

		priv->viu.osd2_upd |= UPDATE_FB;
		priv->viu.osd2_addr = gem->paddr;
		priv->viu.osd2_stride = fb->pitches[0];
		priv->viu.osd2_height = fb->height;
		priv->viu.osd2_width = fb->width;
		priv->viu.osd2_wrap = wrap;
	}

	//FIXME: Interlace mode handling
	if (new_state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) {
		
	}
	
	if(priv->vsync_disabled)
	{
		meson_venc_enable_vsync(priv);
			priv->vsync_disabled = false;
	}
	priv->viu.osd2_reset = false;
	priv->cursor_enabled = true;
	priv->viu.osd2_commit = true;
	priv->t_cursor = ktime_get_ns();
	
	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static void nosem_cursor_atomic_async_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);

	swap(plane->state->fb, new_state->fb);
	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->crtc_h = new_state->crtc_h;
	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_w = new_state->src_w;
	plane->state->src_h = new_state->src_h;
	plane->state->alpha = new_state->alpha;
	plane->state->pixel_blend_mode = new_state->pixel_blend_mode;
	plane->state->rotation = new_state->rotation;
	plane->state->zpos = new_state->zpos;
	plane->state->normalized_zpos = new_state->normalized_zpos;
	plane->state->color_encoding = new_state->color_encoding;
	plane->state->color_range = new_state->color_range;
	plane->state->src = new_state->src;
	plane->state->dst = new_state->dst;
	plane->state->visible = new_state->visible;
    
	nosem_cursor_atomic_update(plane,state);
}

static void nosem_cursor_atomic_disable(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct nosem_cursor *nosem_cursor = to_nosem_cursor(plane);
	struct meson_drm *priv = nosem_cursor->priv;

	/* Disable OSD2 */
	writel_bits_relaxed(OSD_BLEND_POSTBLD_SRC_OSD2, 0,
			    priv->io_base + _REG(OSD2_BLEND_SRC_CTRL));
	if(priv->vsync_disabled)
		meson_venc_disable_vsync(priv);
	priv->cursor_enabled = false;
	priv->viu.osd2_enabled = false;
}

static const struct drm_plane_helper_funcs nosem_cursor_helper_funcs = {
	.atomic_check	= nosem_cursor_atomic_check,
	.atomic_disable	= nosem_cursor_atomic_disable,
	.atomic_update	= nosem_cursor_atomic_update,
	.atomic_async_check	= nosem_cursor_atomic_async_check,
	.atomic_async_update = nosem_cursor_atomic_async_update,
};

static const struct drm_plane_funcs nosem_cursor_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const uint32_t supported_drm_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
};

static const uint64_t format_modifiers_default[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

int nosem_cursor_create(struct meson_drm *priv)
{
	struct nosem_cursor *nosem_cursor;
	struct drm_plane *cursor;

	nosem_cursor = devm_kzalloc(priv->drm->dev, sizeof(*nosem_cursor),
				   GFP_KERNEL);
	if (!nosem_cursor)
		return -ENOMEM;

	nosem_cursor->priv = priv;
	cursor = &nosem_cursor->base;

	drm_universal_plane_init(priv->drm, cursor, 0xFF,
				 &nosem_cursor_funcs,
				 supported_drm_formats,
				 ARRAY_SIZE(supported_drm_formats),
				 format_modifiers_default,
				 DRM_PLANE_TYPE_CURSOR, NULL);

	drm_plane_helper_add(cursor, &nosem_cursor_helper_funcs);

	/* For now, OSD Primary plane is always on the front */
	drm_plane_create_zpos_immutable_property(cursor, 2);

	priv->cursor_plane = cursor;

	return 0;
}
