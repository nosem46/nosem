// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 */

#include <linux/export.h>
#include <linux/bitfield.h>

#include <drm/drm_fourcc.h>

#include "meson_drv.h"
#include "meson_viu.h"
#include "meson_registers.h"

/**
 * DOC: Video Input Unit
 *
 * VIU Handles the Pixel scanout and the basic Colorspace conversions
 * We handle the following features :
 *
 * - OSD1 RGB565/RGB888/xRGB8888 scanout
 * - RGB conversion to x/cb/cr
 * - Progressive or Interlace buffer scanout
 * - OSD1 Commit on Vsync
 * - HDR OSD matrix for GXL/GXM
 *
 * What is missing :
 *
 * - BGR888/xBGR8888/BGRx8888/BGRx8888 modes
 * - YUV4:2:2 Y0CbY1Cr scanout
 * - Conversion to YUV 4:4:4 from 4:2:2 input
 * - Colorkey Alpha matching
 * - Big endian scanout
 * - X/Y reverse scanout
 * - Global alpha setup
 * - OSD2 support, would need interlace switching on vsync
 * - OSD1 full scaling to support TV overscan
 */

#define COEFF_NORM(a) ((int)((((a) * 2048.0) + 1) / 2))
#define MATRIX_5X3_COEF_SIZE 24

static int RGB709_to_YUV709l_coeff[MATRIX_5X3_COEF_SIZE] = {
	0, 0, 0, /* pre offset */
	COEFF_NORM(0.181873),	COEFF_NORM(0.611831),	COEFF_NORM(0.061765),
	COEFF_NORM(-0.100251),	COEFF_NORM(-0.337249),	COEFF_NORM(0.437500),
	COEFF_NORM(0.437500),	COEFF_NORM(-0.397384),	COEFF_NORM(-0.040116),
	0, 0, 0, /* 10'/11'/12' */
	0, 0, 0, /* 20'/21'/22' */
	64, 512, 512, /* offset */
	0, 0, 0 /* mode, right_shift, clip_en */
};

static void meson_viu_set_g12a_osd_matrix(struct meson_drm *priv,
					   int *m, bool csc_on)
{
	/* VPP WRAP OSD1 matrix */
	writel(((m[0] & 0xfff) << 16) | (m[1] & 0xfff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_PRE_OFFSET0_1));
	writel(m[2] & 0xfff,
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_PRE_OFFSET2));
	writel(((m[3] & 0x1fff) << 16) | (m[4] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_COEF00_01));
	writel(((m[5] & 0x1fff) << 16) | (m[6] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_COEF02_10));
	writel(((m[7] & 0x1fff) << 16) | (m[8] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_COEF11_12));
	writel(((m[9] & 0x1fff) << 16) | (m[10] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_COEF20_21));
	writel((m[11] & 0x1fff),
		priv->io_base +	_REG(VPP_WRAP_OSD1_MATRIX_COEF22));

	writel(((m[18] & 0xfff) << 16) | (m[19] & 0xfff),
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_OFFSET0_1));
	writel(m[20] & 0xfff,
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_OFFSET2));

	writel_bits_relaxed(BIT(0), csc_on ? BIT(0) : 0,
		priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_EN_CTRL));
    
	/* VPP WRAP OSD2 matrix */
	writel(((m[0] & 0xfff) << 16) | (m[1] & 0xfff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_PRE_OFFSET0_1));
	writel(m[2] & 0xfff,
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_PRE_OFFSET2));
	writel(((m[3] & 0x1fff) << 16) | (m[4] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_COEF00_01));
	writel(((m[5] & 0x1fff) << 16) | (m[6] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_COEF02_10));
	writel(((m[7] & 0x1fff) << 16) | (m[8] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_COEF11_12));
	writel(((m[9] & 0x1fff) << 16) | (m[10] & 0x1fff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_COEF20_21));
	writel((m[11] & 0x1fff),
		priv->io_base +	_REG(VPP_WRAP_OSD2_MATRIX_COEF22));

	writel(((m[18] & 0xfff) << 16) | (m[19] & 0xfff),
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_OFFSET0_1));
	writel(m[20] & 0xfff,
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_OFFSET2));

	writel_bits_relaxed(BIT(0), csc_on ? BIT(0) : 0,
		priv->io_base + _REG(VPP_WRAP_OSD2_MATRIX_EN_CTRL));
}

/* VIU OSD1 Reset as workaround for GXL+ Alpha OSD Bug */
void meson_viu_osd1_reset(struct meson_drm *priv)
{
}

#define OSD1_MALI_ORDER_ABGR				\
	(FIELD_PREP(VIU_OSD1_MALI_AFBCD_A_REORDER,	\
		    VIU_OSD1_MALI_REORDER_A) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_B_REORDER,	\
		    VIU_OSD1_MALI_REORDER_B) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_G_REORDER,	\
		    VIU_OSD1_MALI_REORDER_G) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_R_REORDER,	\
		    VIU_OSD1_MALI_REORDER_R))

#define OSD1_MALI_ORDER_ARGB				\
	(FIELD_PREP(VIU_OSD1_MALI_AFBCD_A_REORDER,	\
		    VIU_OSD1_MALI_REORDER_A) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_B_REORDER,	\
		    VIU_OSD1_MALI_REORDER_R) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_G_REORDER,	\
		    VIU_OSD1_MALI_REORDER_G) |		\
	 FIELD_PREP(VIU_OSD1_MALI_AFBCD_R_REORDER,	\
		    VIU_OSD1_MALI_REORDER_B))

void meson_viu_g12a_enable_osd1_afbc(struct meson_drm *priv)
{
	u32 afbc_order = OSD1_MALI_ORDER_ARGB;

	/* Enable Mali AFBC Unpack */
	writel_bits_relaxed(VIU_OSD1_MALI_UNPACK_EN,
			    VIU_OSD1_MALI_UNPACK_EN,
			    priv->io_base + _REG(VIU_OSD1_MALI_UNPACK_CTRL));

	switch (priv->afbcd.format) {
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		afbc_order = OSD1_MALI_ORDER_ABGR;
		break;
	}

	/* Setup RGBA Reordering */
	writel_bits_relaxed(VIU_OSD1_MALI_AFBCD_A_REORDER |
			    VIU_OSD1_MALI_AFBCD_B_REORDER |
			    VIU_OSD1_MALI_AFBCD_G_REORDER |
			    VIU_OSD1_MALI_AFBCD_R_REORDER,
			    afbc_order,
			    priv->io_base + _REG(VIU_OSD1_MALI_UNPACK_CTRL));

	/* Select AFBCD path for OSD1 */
	writel_bits_relaxed(OSD_PATH_OSD_AXI_SEL_OSD1_AFBCD,
			    OSD_PATH_OSD_AXI_SEL_OSD1_AFBCD,
			    priv->io_base + _REG(OSD_PATH_MISC_CTRL));
}

void meson_viu_g12a_disable_osd1_afbc(struct meson_drm *priv)
{
	/* Disable AFBCD path for OSD1 */
	writel_bits_relaxed(OSD_PATH_OSD_AXI_SEL_OSD1_AFBCD, 0,
			    priv->io_base + _REG(OSD_PATH_MISC_CTRL));

	/* Disable AFBCD unpack */
	writel_bits_relaxed(VIU_OSD1_MALI_UNPACK_EN, 0,
			    priv->io_base + _REG(VIU_OSD1_MALI_UNPACK_CTRL));
}

void meson_viu_init(struct meson_drm *priv)
{
	uint32_t reg;

	/* Disable OSDs */
	writel_bits_relaxed(VIU_OSD1_OSD_BLK_ENABLE | VIU_OSD1_OSD_ENABLE, 0,
			    priv->io_base + _REG(VIU_OSD1_CTRL_STAT));
	writel_bits_relaxed(VIU_OSD1_OSD_BLK_ENABLE | VIU_OSD1_OSD_ENABLE, 0,
			    priv->io_base + _REG(VIU_OSD2_CTRL_STAT));

	meson_viu_set_g12a_osd_matrix(priv, RGB709_to_YUV709l_coeff,
				       true);
	/* fix green/pink color distortion from vendor u-boot */
	writel_bits_relaxed(OSD1_HDR2_CTRL_REG_ONLY_MAT |
			OSD1_HDR2_CTRL_VDIN0_HDR2_TOP_EN, 0,
			priv->io_base + _REG(OSD1_HDR2_CTRL));

	/* Initialize OSD1 fifo control register */
	reg = VIU_OSD_DDR_PRIORITY_URGENT |
		VIU_OSD_FIFO_DEPTH_VAL(32) | /* fifo_depth_val: 32*8=256 */
		VIU_OSD_WORDS_PER_BURST(4) | /* 4 words in 1 burst */
		VIU_OSD_FIFO_LIMITS(2);      /* fifo_lim: 2*16=32 */

	reg |= (VIU_OSD_BURST_LENGTH_32 | VIU_OSD_HOLD_FIFO_LINES(31));

	writel_relaxed(reg, priv->io_base + _REG(VIU_OSD1_FIFO_CTRL_STAT));
	writel_relaxed(reg, priv->io_base + _REG(VIU_OSD2_FIFO_CTRL_STAT));

	/* Set OSD alpha replace value */
	writel_bits_relaxed(0xff << OSD_REPLACE_SHIFT,
			    0xff << OSD_REPLACE_SHIFT,
			    priv->io_base + _REG(VIU_OSD1_CTRL_STAT2));
	writel_bits_relaxed(0xff << OSD_REPLACE_SHIFT,
			    0xff << OSD_REPLACE_SHIFT,
			    priv->io_base + _REG(VIU_OSD2_CTRL_STAT2));

	/* Disable VD1 AFBC */
	/* di_mif0_en=0 mif0_to_vpp_en=0 di_mad_en=0 and afbc vd1 set=0*/
	writel_bits_relaxed(VIU_CTRL0_VD1_AFBC_MASK, 0,
			    priv->io_base + _REG(VIU_MISC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(AFBC_ENABLE));

	writel_relaxed(0x00FF00C0,
			priv->io_base + _REG(VD1_IF0_LUMA_FIFO_SIZE));
	writel_relaxed(0x00FF00C0,
			priv->io_base + _REG(VD2_IF0_LUMA_FIFO_SIZE));


	u32 val = (u32)VIU_OSD_BLEND_REORDER(0, 1) |
		  (u32)VIU_OSD_BLEND_REORDER(1, 4) |
		  (u32)VIU_OSD_BLEND_REORDER(2, 4) |
		  (u32)VIU_OSD_BLEND_REORDER(3, 2) |
		  (u32)VIU_OSD_BLEND_DIN_EN(9) |
		  (u32)VIU_OSD_BLEND_DIN0_BYPASS_TO_DOUT0 |
		  (u32)VIU_OSD_BLEND_BLEN2_PREMULT_EN(1) |
		  (u32)VIU_OSD_BLEND_HOLD_LINES(4);
	writel_relaxed(val, priv->io_base + _REG(VIU_OSD_BLEND_CTRL));

	writel_relaxed(OSD_BLEND_PATH_SEL_ENABLE,
		       priv->io_base + _REG(OSD1_BLEND_SRC_CTRL));
	writel_relaxed(OSD_BLEND_PATH_SEL_ENABLE,
		       priv->io_base + _REG(OSD2_BLEND_SRC_CTRL));
	writel_relaxed(0, priv->io_base + _REG(VD1_BLEND_SRC_CTRL));
	writel_relaxed(0, priv->io_base + _REG(VD2_BLEND_SRC_CTRL));
	writel_relaxed(0,
			priv->io_base + _REG(VIU_OSD_BLEND_DUMMY_DATA0));
	writel_relaxed(0,
			priv->io_base + _REG(VIU_OSD_BLEND_DUMMY_ALPHA));

	writel_bits_relaxed(DOLBY_BYPASS_EN(0xc), DOLBY_BYPASS_EN(0xc),
			    priv->io_base + _REG(DOLBY_PATH_CTRL));

	meson_viu_g12a_disable_osd1_afbc(priv);

	priv->viu.osd1_enabled = false;
	priv->viu.osd1_commit = false;
	priv->viu.osd1_interlace = false;

	priv->viu.osd2_enabled = false;
	priv->viu.osd2_commit = false;
}
