// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <dt-bindings/reset/stih407-resets.h>
#include "reset-syscfg.h"

/* STiH407 Peripheral powerdown definitions. */
static const char stih407_core[] = "st,stih407-core-syscfg";
static const char stih407_sbc_reg[] = "st,stih407-sbc-reg-syscfg";
static const char stih407_lpm[] = "st,stih407-lpm-syscfg";

#define STIH407_PDN_0(_bit) \
	_SYSCFG_RST_CH(stih407_core, SYSCFG_5000, _bit, SYSSTAT_5500, _bit)
#define STIH407_PDN_1(_bit) \
	_SYSCFG_RST_CH(stih407_core, SYSCFG_5001, _bit, SYSSTAT_5501, _bit)
#define STIH407_PDN_ETH(_bit, _stat) \
	_SYSCFG_RST_CH(stih407_sbc_reg, SYSCFG_4032, _bit, SYSSTAT_4520, _stat)

/* Powerdown requests control 0 */
#define SYSCFG_5000	0x0
#define SYSSTAT_5500	0x7d0
/* Powerdown requests control 1 (High Speed Links) */
#define SYSCFG_5001	0x4
#define SYSSTAT_5501	0x7d4

/* Ethernet powerdown/status/reset */
#define SYSCFG_4032	0x80
#define SYSSTAT_4520	0x820
#define SYSCFG_4002	0x8

static const struct syscfg_reset_channel_data stih407_powerdowns[] = {
	[STIH407_EMISS_POWERDOWN] = STIH407_PDN_0(1),
	[STIH407_NAND_POWERDOWN] = STIH407_PDN_0(0),
	[STIH407_USB3_POWERDOWN] = STIH407_PDN_1(6),
	[STIH407_USB2_PORT1_POWERDOWN] = STIH407_PDN_1(5),
	[STIH407_USB2_PORT0_POWERDOWN] = STIH407_PDN_1(4),
	[STIH407_PCIE1_POWERDOWN] = STIH407_PDN_1(3),
	[STIH407_PCIE0_POWERDOWN] = STIH407_PDN_1(2),
	[STIH407_SATA1_POWERDOWN] = STIH407_PDN_1(1),
	[STIH407_SATA0_POWERDOWN] = STIH407_PDN_1(0),
	[STIH407_ETH1_POWERDOWN] = STIH407_PDN_ETH(0, 2),
};

/* Reset Generator control 0/1 */
#define LPM_SYSCFG_1	0x4	/* Softreset IRB & SBC UART */

#define STIH407_SRST_SBC(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih407_sbc_reg, _reg, _bit)

#define STIH407_SRST_LPM(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih407_lpm, _reg, _bit)

static const struct syscfg_reset_channel_data stih407_softresets[] = {
	[STIH407_ETH1_SOFTRESET] = STIH407_SRST_SBC(SYSCFG_4002, 4),
	[STIH407_IRB_SOFTRESET] = STIH407_SRST_LPM(LPM_SYSCFG_1, 6),
	[STIH407_LPM_SOFTRESET] = STIH407_SRST_SBC(SYSCFG_4002, 2),
	[STIH407_KEYSCAN_SOFTRESET] = STIH407_SRST_LPM(LPM_SYSCFG_1, 8),
};

static const struct syscfg_reset_controller_data stih407_powerdown_controller = {
	.wait_for_ack = true,
	.nr_channels = ARRAY_SIZE(stih407_powerdowns),
	.channels = stih407_powerdowns,
};

static const struct syscfg_reset_controller_data stih407_softreset_controller = {
	.wait_for_ack = false,
	.active_low = true,
	.nr_channels = ARRAY_SIZE(stih407_softresets),
	.channels = stih407_softresets,
};

static const struct of_device_id stih407_reset_match[] = {
	{
		.compatible = "st,stih407-powerdown",
		.data = &stih407_powerdown_controller,
	},
	{
		.compatible = "st,stih407-softreset",
		.data = &stih407_softreset_controller,
	},
	{ /* sentinel */ },
};

static struct platform_driver stih407_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		.name = "reset-stih407",
		.of_match_table = stih407_reset_match,
	},
};

static int __init stih407_reset_init(void)
{
	return platform_driver_register(&stih407_reset_driver);
}

arch_initcall(stih407_reset_init);
