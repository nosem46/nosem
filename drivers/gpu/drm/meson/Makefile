# SPDX-License-Identifier: GPL-2.0-only
ifeq ($(CONFIG_DRM_NOSEM),)
	meson-drm-y := meson_drv.o meson_crtc.o meson_plane.o meson_overlay.o meson_viu.o 
else
	meson-drm-y := nosem_drv.o nosem_crtc.o nosem_plane.o nosem_overlay.o nosem_viu.o nosem_cursor.o
endif

meson-drm-y += meson_encoder_cvbs.o
meson-drm-y += meson_vpp.o meson_venc.o meson_vclk.o
meson-drm-y += meson_rdma.o meson_osd_afbcd.o
meson-drm-y += meson_encoder_hdmi.o meson_encoder_dsi.o

obj-$(CONFIG_DRM_MESON) += meson-drm.o
obj-$(CONFIG_DRM_MESON_DW_HDMI) += meson_dw_hdmi.o
obj-$(CONFIG_DRM_MESON_DW_MIPI_DSI) += meson_dw_mipi_dsi.o
