// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 */

#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>

#include "sti_compositor.h"
#include "sti_gdp.h"
#include "sti_plane.h"
#include "sti_vtg.h"

#define ALPHASWITCH     BIT(6)
#define ENA_COLOR_FILL  BIT(8)
#define BIGNOTLITTLE    BIT(23)
#define WAIT_NEXT_VSYNC BIT(31)

/* GDP color formats */
#define GDP_RGB565      0x00
#define GDP_RGB888      0x01
#define GDP_RGB888_32   0x02
#define GDP_XBGR8888    (GDP_RGB888_32 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB8565    0x04
#define GDP_ARGB8888    0x05
#define GDP_ABGR8888    (GDP_ARGB8888 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB1555    0x06
#define GDP_ARGB4444    0x07

#define GDP2STR(fmt) { GDP_ ## fmt, #fmt }

static struct gdp_format_to_str {
	int format;
	char name[20];
} gdp_format_to_str[] = {
		GDP2STR(RGB565),
		GDP2STR(RGB888),
		GDP2STR(RGB888_32),
		GDP2STR(XBGR8888),
		GDP2STR(ARGB8565),
		GDP2STR(ARGB8888),
		GDP2STR(ABGR8888),
		GDP2STR(ARGB1555),
		GDP2STR(ARGB4444)
		};

/* GDP register offsets */
#define GAM_GDP_CTL_OFFSET      0x00
#define GAM_GDP_AGC_OFFSET      0x04
#define GAM_GDP_VPO_OFFSET      0x0C
#define GAM_GDP_VPS_OFFSET      0x10
#define GAM_GDP_PML_OFFSET      0x14
#define GAM_GDP_PMP_OFFSET      0x18
#define GAM_GDP_SIZE_OFFSET     0x1C
#define GAM_GDP_NVN_OFFSET      0x24
#define GAM_GDP_KEY1_OFFSET     0x28
#define GAM_GDP_KEY2_OFFSET     0x2C
#define GAM_GDP_PPT_OFFSET      0x34
#define GAM_GDP_CML_OFFSET      0x3C
#define GAM_GDP_NODE_SIZE	0x40
#define GAM_GDP_MST_OFFSET      0x68

/* GDPPLUS register offsets */
#define	GAM_GDPPLUS_CTL_OFFSET	0x00
#define	GAM_GDPPLUS_AGC_OFFSET	0x04
#define	GAM_GDPPLUS_VPO_OFFSET	0x08
#define	GAM_GDPPLUS_VPS_OFFSET	0x0C
#define	GAM_GDPPLUS_PML_OFFSET	0x10
#define	GAM_GDPPLUS_PMP_OFFSET	0x14
#define	GAM_GDPPLUS_SIZE_OFFSET	0x18
#define	GAM_GDPPLUS_NVN_OFFSET	0x1C
#define	GAM_GDPPLUS_KEY1_OFFSET	0x20
#define	GAM_GDPPLUS_KEY2_OFFSET	0x24
#define	GAM_GDPPLUS_HFP_OFFSET	0x28
#define	GAM_GDPPLUS_PPT_OFFSET	0x2C
#define	GAM_GDPPLUS_VFP_OFFSET	0x30
#define	GAM_GDPPLUS_CML_OFFSET	0x34
#define	GAM_GDPPLUS_CROP_OFFSET	0x38
#define	GAM_GDPPLUS_BT0_OFFSET	0x3C
#define	GAM_GDPPLUS_BT1_OFFSET	0x40
#define	GAM_GDPPLUS_BT2_OFFSET	0x44
#define	GAM_GDPPLUS_BT3_OFFSET	0x48
#define	GAM_GDPPLUS_BT4_OFFSET	0x4C
#define	GAM_GDPPLUS_HSRC_OFFSET	0x50
#define	GAM_GDPPLUS_HIP_OFFSET	0x54
#define	GAM_GDPPLUS_HP1_OFFSET	0x58
#define	GAM_GDPPLUS_HP2_OFFSET	0x5C
#define	GAM_GDPPLUS_VSRC_OFFSET	0x60
#define	GAM_GDPPLUS_VIP_OFFSET	0x64
#define	GAM_GDPPLUS_VP1_OFFSET	0x68
#define	GAM_GDPPLUS_VP2_OFFSET	0x6C
#define GAM_GDPPLUS_NODE_SIZE	0x500

/* Accessor for common registers */
#define GAM_OFFSET(reg, type)	((type) == STI_GDP_TYPE_GDP ? GAM_GDP_ ## reg ## _OFFSET :\
				 GAM_GDPPLUS_ ## reg ## _OFFSET)
#define GAM_OFFSET_U32(reg, type)	(GAM_OFFSET(reg, type) >> 2)

#define GAM_GDP_ALPHARANGE_255  BIT(5)
#define GAM_GDP_AGC_FULL_RANGE  0x00808080
#define GAM_GDP_PPT_IGNORE      (BIT(1) | BIT(0))

#define GAM_GDP_SIZE_MAX_WIDTH  3840
#define GAM_GDP_SIZE_MAX_HEIGHT 2160

#define GDP_NODE_NB_BANK        2
#define GDP_NODE_PER_FIELD      2

struct sti_gdp_node_list {
	u32 *top_field;
	dma_addr_t top_field_paddr;
	u32 *btm_field;
	dma_addr_t btm_field_paddr;
};

/*
 * STI GDP structure
 *
 * @sti_plane:          sti_plane structure
 * @dev:                driver device
 * @regs:               gdp registers
 * @clk_pix:            pixel clock for the current gdp
 * @clk_main_parent:    gdp parent clock if main path used
 * @clk_aux_parent:     gdp parent clock if aux path used
 * @vtg_field_nb:       callback for VTG FIELD (top or bottom) notification
 * @is_curr_top:        true if the current node processed is the top field
 * @node_list:          array of node list
 * @vtg:                registered vtg
 */
struct sti_gdp {
	struct sti_plane plane;
	struct device *dev;
	void __iomem *regs;
	struct clk *clk_pix;
	struct clk *clk_main_parent;
	struct clk *clk_aux_parent;
	struct notifier_block vtg_field_nb;
	bool is_curr_top;
	struct sti_gdp_node_list node_list[GDP_NODE_NB_BANK];
	struct sti_vtg *vtg;
	enum sti_gdp_type type;
};

#define to_sti_gdp(x) container_of(x, struct sti_gdp, plane)

static const uint32_t gdp_supported_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
};

#define DBGFS_DUMP(reg, offset) seq_printf(s, "\n  %-25s 0x%08X", #reg, \
					   readl(gdp->regs + (offset)))

static void gdp_dbg_ctl(struct seq_file *s, int val)
{
	int i;

	seq_puts(s, "\tColor:");
	for (i = 0; i < ARRAY_SIZE(gdp_format_to_str); i++) {
		if (gdp_format_to_str[i].format == (val & 0x1F)) {
			seq_puts(s, gdp_format_to_str[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(gdp_format_to_str))
		seq_puts(s, "<UNKNOWN>");

	seq_printf(s, "\tWaitNextVsync:%d", val & WAIT_NEXT_VSYNC ? 1 : 0);
}

static void gdp_dbg_vpo(struct seq_file *s, int val)
{
	seq_printf(s, "\txdo:%4d\tydo:%4d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_vps(struct seq_file *s, int val)
{
	seq_printf(s, "\txds:%4d\tyds:%4d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_size(struct seq_file *s, int val)
{
	seq_printf(s, "\t%d x %d", val & 0xFFFF, (val >> 16) & 0xFFFF);
}

static void gdp_dbg_nvn(struct seq_file *s, struct sti_gdp *gdp, int val)
{
	void *base = NULL;
	unsigned int i;

	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		if (gdp->node_list[i].top_field_paddr == val) {
			base = gdp->node_list[i].top_field;
			break;
		}
		if (gdp->node_list[i].btm_field_paddr == val) {
			base = gdp->node_list[i].btm_field;
			break;
		}
	}

	if (base)
		seq_printf(s, "\tVirt @: %p", base);
}

static void gdp_dbg_ppt(struct seq_file *s, int val)
{
	if (val & GAM_GDP_PPT_IGNORE)
		seq_puts(s, "\tNot displayed on mixer!");
}

static void gdp_dbg_mst(struct seq_file *s, int val)
{
	if (val & 1)
		seq_puts(s, "\tBUFFER UNDERFLOW!");
}

static int gdp_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_gdp *gdp = (struct sti_gdp *)node->info_ent->data;
	struct device_node *np = gdp->dev->of_node;
	struct drm_plane *drm_plane = &gdp->plane.drm_plane;
	struct drm_crtc *crtc;

	drm_modeset_lock(&drm_plane->mutex, NULL);
	crtc = drm_plane->state->crtc;
	drm_modeset_unlock(&drm_plane->mutex);

	seq_printf(s, "%s: (vaddr = 0x%p)",
		   sti_plane_to_str(&gdp->plane), gdp->regs);

	DBGFS_DUMP(CTL, GAM_OFFSET(CTL, gdp->type));
	gdp_dbg_ctl(s, readl(gdp->regs + GAM_OFFSET(CTL, gdp->type)));
	DBGFS_DUMP(AGC, GAM_OFFSET(AGC, gdp->type));
	DBGFS_DUMP(VPO, GAM_OFFSET(VPO, gdp->type));
	gdp_dbg_vpo(s, readl(gdp->regs + GAM_OFFSET(VPO, gdp->type)));
	DBGFS_DUMP(VPS, GAM_OFFSET(VPS, gdp->type));
	gdp_dbg_vps(s, readl(gdp->regs + GAM_OFFSET(VPS, gdp->type)));
	DBGFS_DUMP(PML, GAM_OFFSET(PML, gdp->type));
	DBGFS_DUMP(PMP, GAM_OFFSET(PMP, gdp->type));
	DBGFS_DUMP(SIZE, GAM_OFFSET(SIZE, gdp->type));
	gdp_dbg_size(s, readl(gdp->regs + GAM_OFFSET(SIZE, gdp->type)));
	DBGFS_DUMP(NVN, GAM_OFFSET(NVN, gdp->type));
	gdp_dbg_nvn(s, gdp, readl(gdp->regs + GAM_OFFSET(NVN, gdp->type)));
	DBGFS_DUMP(KEY1, GAM_OFFSET(KEY1, gdp->type));
	DBGFS_DUMP(KEY2, GAM_OFFSET(KEY2, gdp->type));
	DBGFS_DUMP(PPT, GAM_OFFSET(PPT, gdp->type));
	gdp_dbg_ppt(s, readl(gdp->regs + GAM_OFFSET(PPT, gdp->type)));
	DBGFS_DUMP(CML, GAM_OFFSET(CML, gdp->type));
	if (of_device_is_compatible(np, "st,stih407-compositor")) {
		DBGFS_DUMP(MST, GAM_GDP_MST_OFFSET);
		gdp_dbg_mst(s, readl(gdp->regs + GAM_GDP_MST_OFFSET));
	}

	seq_puts(s, "\n\n");
	if (!crtc)
		seq_puts(s, "  Not connected to any DRM CRTC\n");
	else
		seq_printf(s, "  Connected to DRM CRTC #%d (%s)\n",
			   crtc->base.id, sti_mixer_to_str(to_sti_mixer(crtc)));

	return 0;
}

static void gdp_node_dump_node(struct seq_file *s, u32 *node, enum sti_gdp_type type)
{
	seq_printf(s, "\t@:0x%p", node);
	seq_printf(s, "\n\tCTL  0x%08X", node[GAM_OFFSET_U32(CTL, type)]);
	gdp_dbg_ctl(s, node[GAM_OFFSET_U32(CTL, type)]);
	seq_printf(s, "\n\tAGC  0x%08X", node[GAM_OFFSET_U32(AGC, type)]);
	seq_printf(s, "\n\tVPO  0x%08X", node[GAM_OFFSET_U32(VPO, type)]);
	gdp_dbg_vpo(s, node[GAM_OFFSET_U32(VPO, type)]);
	seq_printf(s, "\n\tVPS  0x%08X", node[GAM_OFFSET_U32(VPS, type)]);
	gdp_dbg_vps(s, node[GAM_OFFSET_U32(VPS, type)]);
	seq_printf(s, "\n\tPML  0x%08X", node[GAM_OFFSET_U32(PML, type)]);
	seq_printf(s, "\n\tPMP  0x%08X", node[GAM_OFFSET_U32(PMP, type)]);
	seq_printf(s, "\n\tSIZE 0x%08X", node[GAM_OFFSET_U32(SIZE, type)]);
	gdp_dbg_size(s, node[GAM_OFFSET_U32(SIZE, type)]);
	seq_printf(s, "\n\tNVN  0x%08X", node[GAM_OFFSET_U32(NVN, type)]);
	seq_printf(s, "\n\tKEY1 0x%08X", node[GAM_OFFSET_U32(KEY1, type)]);
	seq_printf(s, "\n\tKEY2 0x%08X", node[GAM_OFFSET_U32(KEY2, type)]);
	seq_printf(s, "\n\tPPT  0x%08X", node[GAM_OFFSET_U32(PPT, type)]);
	gdp_dbg_ppt(s, node[GAM_OFFSET_U32(PPT, type)]);
	seq_printf(s, "\n\tCML  0x%08X\n", node[GAM_OFFSET_U32(CML, type)]);
}

static int gdp_node_dbg_show(struct seq_file *s, void *arg)
{
	struct drm_info_node *node = s->private;
	struct sti_gdp *gdp = (struct sti_gdp *)node->info_ent->data;
	unsigned int b;

	for (b = 0; b < GDP_NODE_NB_BANK; b++) {
		seq_printf(s, "\n%s[%d].top", sti_plane_to_str(&gdp->plane), b);
		gdp_node_dump_node(s, gdp->node_list[b].top_field, gdp->type);
		seq_printf(s, "\n%s[%d].btm", sti_plane_to_str(&gdp->plane), b);
		gdp_node_dump_node(s, gdp->node_list[b].btm_field, gdp->type);
	}

	return 0;
}

static struct drm_info_list gdp0_debugfs_files[] = {
	{ "gdp0", gdp_dbg_show, 0, NULL },
	{ "gdp0_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp1_debugfs_files[] = {
	{ "gdp1", gdp_dbg_show, 0, NULL },
	{ "gdp1_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp2_debugfs_files[] = {
	{ "gdp2", gdp_dbg_show, 0, NULL },
	{ "gdp2_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp3_debugfs_files[] = {
	{ "gdp3", gdp_dbg_show, 0, NULL },
	{ "gdp3_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp4_debugfs_files[] = {
	{ "gdp4", gdp_dbg_show, 0, NULL },
	{ "gdp4_node", gdp_node_dbg_show, 0, NULL },
};

static struct drm_info_list gdp5_debugfs_files[] = {
	{ "gdp5", gdp_dbg_show, 0, NULL },
	{ "gdp5_node", gdp_node_dbg_show, 0, NULL },
};

static int gdp_debugfs_init(struct sti_gdp *gdp, struct drm_minor *minor)
{
	unsigned int i;
	struct drm_info_list *gdp_debugfs_files;
	int nb_files;

	switch (gdp->plane.desc) {
	case STI_GDP_0:
		gdp_debugfs_files = gdp0_debugfs_files;
		nb_files = ARRAY_SIZE(gdp0_debugfs_files);
		break;
	case STI_GDP_1:
		gdp_debugfs_files = gdp1_debugfs_files;
		nb_files = ARRAY_SIZE(gdp1_debugfs_files);
		break;
	case STI_GDP_2:
		gdp_debugfs_files = gdp2_debugfs_files;
		nb_files = ARRAY_SIZE(gdp2_debugfs_files);
		break;
	case STI_GDP_3:
		gdp_debugfs_files = gdp3_debugfs_files;
		nb_files = ARRAY_SIZE(gdp3_debugfs_files);
		break;
	case STI_GDP_4:
		gdp_debugfs_files = gdp4_debugfs_files;
		nb_files = ARRAY_SIZE(gdp4_debugfs_files);
		break;
	case STI_GDP_5:
		gdp_debugfs_files = gdp5_debugfs_files;
		nb_files = ARRAY_SIZE(gdp5_debugfs_files);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < nb_files; i++)
		gdp_debugfs_files[i].data = gdp;

	drm_debugfs_create_files(gdp_debugfs_files,
				 nb_files,
				 minor->debugfs_root, minor);
	return 0;
}

static int sti_gdp_fourcc2format(int fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_XRGB8888:
		return GDP_RGB888_32;
	case DRM_FORMAT_XBGR8888:
		return GDP_XBGR8888;
	case DRM_FORMAT_ARGB8888:
		return GDP_ARGB8888;
	case DRM_FORMAT_ABGR8888:
		return GDP_ABGR8888;
	case DRM_FORMAT_ARGB4444:
		return GDP_ARGB4444;
	case DRM_FORMAT_ARGB1555:
		return GDP_ARGB1555;
	case DRM_FORMAT_RGB565:
		return GDP_RGB565;
	case DRM_FORMAT_RGB888:
		return GDP_RGB888;
	}
	return -1;
}

static int sti_gdp_get_alpharange(int format)
{
	switch (format) {
	case GDP_ARGB8565:
	case GDP_ARGB8888:
	case GDP_ABGR8888:
		return GAM_GDP_ALPHARANGE_255;
	}
	return 0;
}

/**
 * sti_gdp_get_free_nodes
 * @gdp: gdp pointer
 *
 * Look for a GDP node list that is not currently read by the HW.
 *
 * RETURNS:
 * Pointer to the free GDP node list
 */
static struct sti_gdp_node_list *sti_gdp_get_free_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	u32 nvn_off = GAM_OFFSET(NVN, gdp->type);
	unsigned int i;

	hw_nvn = readl(gdp->regs + nvn_off);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn != gdp->node_list[i].btm_field_paddr) &&
		    (hw_nvn != gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

	/* in hazardous cases restart with the first node */
	DRM_ERROR("inconsistent NVN for %s: 0x%08X\n",
			sti_plane_to_str(&gdp->plane), hw_nvn);

end:
	return &gdp->node_list[0];
}

/**
 * sti_gdp_get_current_nodes
 * @gdp: gdp pointer
 *
 * Look for GDP nodes that are currently read by the HW.
 *
 * RETURNS:
 * Pointer to the current GDP node list
 */
static
struct sti_gdp_node_list *sti_gdp_get_current_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	u32 nvn_off = GAM_OFFSET(NVN, gdp->type);
	unsigned int i;

	hw_nvn = readl(gdp->regs + nvn_off);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn == gdp->node_list[i].btm_field_paddr) ||
				(hw_nvn == gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

end:
	DRM_DEBUG_DRIVER("Warning, NVN 0x%08X for %s does not match any node\n",
				hw_nvn, sti_plane_to_str(&gdp->plane));

	return NULL;
}

/**
 * sti_gdp_disable
 * @gdp: gdp pointer
 *
 * Disable a GDP.
 */
static void sti_gdp_disable(struct sti_gdp *gdp)
{
	unsigned int i;
	u32 ppt_off = GAM_OFFSET_U32(PPT, gdp->type);

	DRM_DEBUG_DRIVER("%s\n", sti_plane_to_str(&gdp->plane));

	/* Set the nodes as 'to be ignored on mixer' */
	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		gdp->node_list[i].top_field[ppt_off] |= GAM_GDP_PPT_IGNORE;
		gdp->node_list[i].btm_field[ppt_off] |= GAM_GDP_PPT_IGNORE;
	}

	if (sti_vtg_unregister_client(gdp->vtg, &gdp->vtg_field_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");

	if (gdp->clk_pix)
		clk_disable_unprepare(gdp->clk_pix);

	gdp->plane.status = STI_PLANE_DISABLED;
	gdp->vtg = NULL;
}

/**
 * sti_gdp_field_cb
 * @nb: notifier block
 * @event: event message
 * @data: private data
 *
 * Handle VTG top field and bottom field event.
 *
 * RETURNS:
 * 0 on success.
 */
static int sti_gdp_field_cb(struct notifier_block *nb,
			    unsigned long event, void *data)
{
	struct sti_gdp *gdp = container_of(nb, struct sti_gdp, vtg_field_nb);

	if (gdp->plane.status == STI_PLANE_FLUSHING) {
		/* disable need to be synchronize on vsync event */
		DRM_DEBUG_DRIVER("Vsync event received => disable %s\n",
				 sti_plane_to_str(&gdp->plane));

		sti_gdp_disable(gdp);
	}

	switch (event) {
	case VTG_TOP_FIELD_EVENT:
		gdp->is_curr_top = true;
		break;
	case VTG_BOTTOM_FIELD_EVENT:
		gdp->is_curr_top = false;
		break;
	default:
		DRM_ERROR("unsupported event: %lu\n", event);
		break;
	}

	return 0;
}

static void sti_gdp_init(struct sti_gdp *gdp)
{
	struct device_node *np = gdp->dev->of_node;
	dma_addr_t dma_addr;
	void *base;
	unsigned int i, size, gdp_node_size;

	/* Check the type of GDP */
	if (gdp->type == STI_GDP_TYPE_GDP)
		gdp_node_size = GAM_GDP_NODE_SIZE;
	else
		gdp_node_size = GAM_GDPPLUS_NODE_SIZE;

	/* Allocate all the nodes within a single memory page */
	size = gdp_node_size * GDP_NODE_PER_FIELD * GDP_NODE_NB_BANK;
	base = dma_alloc_wc(gdp->dev, size, &dma_addr, GFP_KERNEL);

	if (!base) {
		DRM_ERROR("Failed to allocate memory for GDP node\n");
		return;
	}
	memset(base, 0, size);

	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].top_field = base;
		gdp->node_list[i].top_field_paddr = dma_addr;

		DRM_DEBUG_DRIVER("node[%d].top_field=%p\n", i, base);
		base += gdp_node_size;
		dma_addr += gdp_node_size;

		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].btm_field = base;
		gdp->node_list[i].btm_field_paddr = dma_addr;
		DRM_DEBUG_DRIVER("node[%d].btm_field=%p\n", i, base);
		base += gdp_node_size;
		dma_addr += gdp_node_size;
	}

	if (of_device_is_compatible(np, "st,stih407-compositor")) {
		/* GDP of STiH407 chip have its own pixel clock */
		char *clk_name;

		switch (gdp->plane.desc) {
		case STI_GDP_0:
			clk_name = "pix_gdp1";
			break;
		case STI_GDP_1:
			clk_name = "pix_gdp2";
			break;
		case STI_GDP_2:
			clk_name = "pix_gdp3";
			break;
		case STI_GDP_3:
			clk_name = "pix_gdp4";
			break;
		default:
			DRM_ERROR("GDP id not recognized\n");
			return;
		}

		gdp->clk_pix = devm_clk_get(gdp->dev, clk_name);
		if (IS_ERR(gdp->clk_pix))
			DRM_ERROR("Cannot get %s clock\n", clk_name);

		gdp->clk_main_parent = devm_clk_get(gdp->dev, "main_parent");
		if (IS_ERR(gdp->clk_main_parent))
			DRM_ERROR("Cannot get main_parent clock\n");

		gdp->clk_aux_parent = devm_clk_get(gdp->dev, "aux_parent");
		if (IS_ERR(gdp->clk_aux_parent))
			DRM_ERROR("Cannot get aux_parent clock\n");
	}
}

/**
 * sti_gdp_get_dst
 * @dev: device
 * @dst: requested destination size
 * @src: source size
 *
 * Return the cropped / clamped destination size
 *
 * RETURNS:
 * cropped / clamped destination size
 */
static int sti_gdp_get_dst(struct device *dev, int dst, int src)
{
	if (dst == src)
		return dst;

	if (dst < src) {
		dev_dbg(dev, "WARNING: GDP scale not supported, will crop\n");
		return dst;
	}

	dev_dbg(dev, "WARNING: GDP scale not supported, will clamp\n");
	return src;
}

static int sti_gdp_atomic_check(struct drm_plane *drm_plane,
				struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_framebuffer *fb =  new_plane_state->fb;
	struct drm_crtc_state *crtc_state;
	struct sti_mixer *mixer;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	int format;

	/* no need for further checks if the plane is being disabled */
	if (!crtc || !fb)
		return 0;

	mixer = to_sti_mixer(crtc);
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	mode = &crtc_state->mode;
	dst_x = new_plane_state->crtc_x;
	dst_y = new_plane_state->crtc_y;
	dst_w = clamp_val(new_plane_state->crtc_w, 0, mode->hdisplay - dst_x);
	dst_h = clamp_val(new_plane_state->crtc_h, 0, mode->vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = new_plane_state->src_x >> 16;
	src_y = new_plane_state->src_y >> 16;
	src_w = clamp_val(new_plane_state->src_w >> 16, 0,
			  GAM_GDP_SIZE_MAX_WIDTH);
	src_h = clamp_val(new_plane_state->src_h >> 16, 0,
			  GAM_GDP_SIZE_MAX_HEIGHT);

	format = sti_gdp_fourcc2format(fb->format->format);
	if (format == -1) {
		DRM_ERROR("Format not supported by GDP %.4s\n",
			  (char *)&fb->format->format);
		return -EINVAL;
	}

	if (!drm_fb_dma_get_gem_obj(fb, 0)) {
		DRM_ERROR("Can't get DMA GEM object for fb\n");
		return -EINVAL;
	}

	/* Set gdp clock */
	if (mode->clock && gdp->clk_pix) {
		struct clk *clkp;
		int rate = mode->clock * 1000;
		int res;

		/*
		 * According to the mixer used, the gdp pixel clock
		 * should have a different parent clock.
		 */
		if (mixer->id == STI_MIXER_MAIN)
			clkp = gdp->clk_main_parent;
		else
			clkp = gdp->clk_aux_parent;

		if (clkp)
			clk_set_parent(gdp->clk_pix, clkp);

		res = clk_set_rate(gdp->clk_pix, rate);
		if (res < 0) {
			DRM_ERROR("Cannot set rate (%dHz) for gdp\n",
				  rate);
			return -EINVAL;
		}
	}

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
		      sti_plane_to_str(plane),
		      dst_w, dst_h, dst_x, dst_y,
		      src_w, src_h, src_x, src_y);

	return 0;
}

static void sti_gdp_atomic_update(struct drm_plane *drm_plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *oldstate = drm_atomic_get_old_plane_state(state,
									  drm_plane);
	struct drm_plane_state *newstate = drm_atomic_get_new_plane_state(state,
									  drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);
	struct drm_crtc *crtc = newstate->crtc;
	struct drm_framebuffer *fb =  newstate->fb;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	struct drm_gem_dma_object *dma_obj;
	struct sti_gdp_node_list *list;
	struct sti_gdp_node_list *curr_list;
	u32 *top_field, *btm_field;
	u32 dma_updated_top;
	u32 dma_updated_btm;
	int format;
	unsigned int bpp;
	u32 ydo, xdo, yds, xds;

	if (!crtc || !fb)
		return;

	if ((oldstate->fb == newstate->fb) &&
	    (oldstate->crtc_x == newstate->crtc_x) &&
	    (oldstate->crtc_y == newstate->crtc_y) &&
	    (oldstate->crtc_w == newstate->crtc_w) &&
	    (oldstate->crtc_h == newstate->crtc_h) &&
	    (oldstate->src_x == newstate->src_x) &&
	    (oldstate->src_y == newstate->src_y) &&
	    (oldstate->src_w == newstate->src_w) &&
	    (oldstate->src_h == newstate->src_h)) {
		/* No change since last update, do not post cmd */
		DRM_DEBUG_DRIVER("No change, not posting cmd\n");
		plane->status = STI_PLANE_UPDATED;
		return;
	}

	if (!gdp->vtg) {
		struct sti_compositor *compo = dev_get_drvdata(gdp->dev);
		struct sti_mixer *mixer = to_sti_mixer(crtc);

		/* Register gdp callback */
		gdp->vtg = compo->vtg[mixer->id];
		sti_vtg_register_client(gdp->vtg, &gdp->vtg_field_nb, crtc);
		clk_prepare_enable(gdp->clk_pix);
	}

	mode = &crtc->mode;
	dst_x = newstate->crtc_x;
	dst_y = newstate->crtc_y;
	dst_w = clamp_val(newstate->crtc_w, 0, mode->hdisplay - dst_x);
	dst_h = clamp_val(newstate->crtc_h, 0, mode->vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = newstate->src_x >> 16;
	src_y = newstate->src_y >> 16;
	src_w = clamp_val(newstate->src_w >> 16, 0, GAM_GDP_SIZE_MAX_WIDTH);
	src_h = clamp_val(newstate->src_h >> 16, 0, GAM_GDP_SIZE_MAX_HEIGHT);

	list = sti_gdp_get_free_nodes(gdp);
	top_field = list->top_field;
	btm_field = list->btm_field;

	dev_dbg(gdp->dev, "%s %s top_node:0x%p btm_node:0x%p\n", __func__,
		sti_plane_to_str(plane), top_field, btm_field);

	/* build the top field */
	top_field[GAM_OFFSET_U32(AGC, gdp->type)] = GAM_GDP_AGC_FULL_RANGE;
	top_field[GAM_OFFSET_U32(CTL, gdp->type)] = WAIT_NEXT_VSYNC;
	format = sti_gdp_fourcc2format(fb->format->format);
	top_field[GAM_OFFSET_U32(CTL, gdp->type)] |= format;
	top_field[GAM_OFFSET_U32(CTL, gdp->type)] |= sti_gdp_get_alpharange(format);
	top_field[GAM_OFFSET_U32(PPT, gdp->type)] &= ~GAM_GDP_PPT_IGNORE;

	dma_obj = drm_fb_dma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&fb->format->format,
			 (unsigned long) dma_obj->dma_addr);

	/* pixel memory location */
	bpp = fb->format->cpp[0];
	top_field[GAM_OFFSET_U32(PML, gdp->type)] = (u32)dma_obj->dma_addr + fb->offsets[0];
	top_field[GAM_OFFSET_U32(PML, gdp->type)] += src_x * bpp;
	top_field[GAM_OFFSET_U32(PML, gdp->type)] += src_y * fb->pitches[0];

	/* output parameters (clamped / cropped) */
	dst_w = sti_gdp_get_dst(gdp->dev, dst_w, src_w);
	dst_h = sti_gdp_get_dst(gdp->dev, dst_h, src_h);
	ydo = sti_vtg_get_line_number(*mode, dst_y);
	yds = sti_vtg_get_line_number(*mode, dst_y + dst_h - 1);
	xdo = sti_vtg_get_pixel_number(*mode, dst_x);
	xds = sti_vtg_get_pixel_number(*mode, dst_x + dst_w - 1);
	top_field[GAM_OFFSET_U32(VPO, gdp->type)] = (ydo << 16) | xdo;
	top_field[GAM_OFFSET_U32(VPS, gdp->type)] = (yds << 16) | xds;

	/* input parameters */
	src_w = dst_w;
	top_field[GAM_OFFSET_U32(PMP, gdp->type)] = fb->pitches[0];
	top_field[GAM_OFFSET_U32(SIZE, gdp->type)] = src_h << 16 | src_w;

	/* Same content and chained together */
	memcpy(btm_field, top_field,
	       gdp->type == STI_GDP_TYPE_GDP ?
	       GAM_GDP_NODE_SIZE : GAM_GDPPLUS_NODE_SIZE);
	top_field[GAM_OFFSET_U32(NVN, gdp->type)] = list->btm_field_paddr;
	btm_field[GAM_OFFSET_U32(NVN, gdp->type)] = list->top_field_paddr;

	/* Interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		btm_field[GAM_OFFSET_U32(PML, gdp->type)] =
			top_field[GAM_OFFSET_U32(PML, gdp->type)] + fb->pitches[0];

	/* Update the NVN field of the 'right' field of the current GDP node
	 * (being used by the HW) with the address of the updated ('free') top
	 * field GDP node.
	 * - In interlaced mode the 'right' field is the bottom field as we
	 *   update frames starting from their top field
	 * - In progressive mode, we update both bottom and top fields which
	 *   are equal nodes.
	 * At the next VSYNC, the updated node list will be used by the HW.
	 */
	curr_list = sti_gdp_get_current_nodes(gdp);
	dma_updated_top = list->top_field_paddr;
	dma_updated_btm = list->btm_field_paddr;

	dev_dbg(gdp->dev, "Current NVN:0x%X\n",
		readl(gdp->regs + GAM_OFFSET(NVN, gdp->type)));
	dev_dbg(gdp->dev, "Posted buff: %lx current buff: %x\n",
		(unsigned long) dma_obj->dma_addr,
		readl(gdp->regs + GAM_OFFSET(PML, gdp->type)));

	if (!curr_list) {
		/* First update or invalid node should directly write in the
		 * hw register */
		DRM_DEBUG_DRIVER("%s first update (or invalid node)\n",
				 sti_plane_to_str(plane));

		writel(gdp->is_curr_top ?
				dma_updated_btm : dma_updated_top,
				gdp->regs + GAM_OFFSET(NVN, gdp->type));
		goto end;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (gdp->is_curr_top) {
			/* Do not update in the middle of the frame, but
			 * postpone the update after the bottom field has
			 * been displayed */
			curr_list->btm_field[GAM_OFFSET_U32(NVN, gdp->type)] = dma_updated_top;
		} else {
			/* Direct update to avoid one frame delay */
			writel(dma_updated_top,
			       gdp->regs + GAM_OFFSET(NVN, gdp->type));
		}
	} else {
		/* Direct update for progressive to avoid one frame delay */
		writel(dma_updated_top, gdp->regs + GAM_OFFSET(NVN, gdp->type));
	}

end:
	sti_plane_update_fps(plane, true, false);

	plane->status = STI_PLANE_UPDATED;
}

static void sti_gdp_atomic_disable(struct drm_plane *drm_plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *oldstate = drm_atomic_get_old_plane_state(state,
									  drm_plane);
	struct sti_plane *plane = to_sti_plane(drm_plane);

	if (!oldstate->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n",
				 drm_plane->base.id);
		return;
	}

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			 oldstate->crtc->base.id,
			 sti_mixer_to_str(to_sti_mixer(oldstate->crtc)),
			 drm_plane->base.id, sti_plane_to_str(plane));

	plane->status = STI_PLANE_DISABLING;
}

static const struct drm_plane_helper_funcs sti_gdp_helpers_funcs = {
	.atomic_check = sti_gdp_atomic_check,
	.atomic_update = sti_gdp_atomic_update,
	.atomic_disable = sti_gdp_atomic_disable,
};

static int sti_gdp_late_register(struct drm_plane *drm_plane)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);

	return gdp_debugfs_init(gdp, drm_plane->dev->primary);
}

static const struct drm_plane_funcs sti_gdp_plane_helpers_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.late_register = sti_gdp_late_register,
};

struct drm_plane *sti_gdp_create(struct drm_device *drm_dev,
				 struct device *dev, enum sti_gdp_type gdp_type, int desc,
				 void __iomem *baseaddr,
				 unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	struct sti_gdp *gdp;
	int res;

	gdp = devm_kzalloc(dev, sizeof(*gdp), GFP_KERNEL);
	if (!gdp) {
		DRM_ERROR("Failed to allocate memory for GDP\n");
		return NULL;
	}

	gdp->dev = dev;
	gdp->regs = baseaddr;
	gdp->plane.desc = desc;
	gdp->plane.status = STI_PLANE_DISABLED;
	gdp->type = gdp_type;

	gdp->vtg_field_nb.notifier_call = sti_gdp_field_cb;

	sti_gdp_init(gdp);

	res = drm_universal_plane_init(drm_dev, &gdp->plane.drm_plane,
				       possible_crtcs,
				       &sti_gdp_plane_helpers_funcs,
				       gdp_supported_formats,
				       ARRAY_SIZE(gdp_supported_formats),
				       NULL, type, NULL);
	if (res) {
		DRM_ERROR("Failed to initialize universal plane\n");
		goto err;
	}

	drm_plane_helper_add(&gdp->plane.drm_plane, &sti_gdp_helpers_funcs);

	sti_plane_init_property(&gdp->plane, type);

	return &gdp->plane.drm_plane;

err:
	devm_kfree(dev, gdp);
	return NULL;
}
