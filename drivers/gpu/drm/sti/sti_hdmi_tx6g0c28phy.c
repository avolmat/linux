// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Alain Volmat - 2023
 * Author: Alain Volmat <avolmat@me.com>
 *
 * This driver is highly inspired from sti_hdmi_tx3g4c28phy.c
 * with IP behavior understood by looking the display package
 * from 4kopen.com
 * https://bitbucket.org/4kopen/display/src/master/display/ip/hdmi/stmhdmitx6g0_c28_phy.cpp
 */

#include <drm/drm_print.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include "sti_hdmi_tx6g0c28phy.h"

#define HDMI_SRZ_CFG		0x504
#define HDMI_SRZ_PWR_CFG	0x508
#define HDMI_SRZ_PLL_CFG	0x510
#define HDMI_SRZ_STR_1		0x518
#define HDMI_SRZ_STR_2		0x51C
#define HDMI_SRZ_CALCODE_EXT	0x530
#define HDMI_SRZ_TX_RSVR_BITS	0x560

/* ******************************* */
/* register : HDMI_SRZ_CFG         */
/* ******************************* */
#define HDMI_SRZ_CFG_EN				BIT(0)
#define HDMI_SRZ_CFG_EN_PE_C0_MASK		GENMASK(6, 4)
#define HDMI_SRZ_CFG_EN_PE_C1_MASK		GENMASK(10, 8)
#define HDMI_SRZ_CFG_EN_PE_C2_MASK		GENMASK(14, 12)

#define HDMI_SRZ_CFG_EXTERNAL_DATA		BIT(16)
#define HDMI_SRZ_CFG_RBIAS_EXT			BIT(17)
#define HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION	BIT(18)
#define HDMI_SRZ_CFG_ISNKCTRL_MASK		GENMASK(21, 20)

#define HDMI_SRZ_CFG_EN_SRC_TERMINATION_SHIFT    (24)
#define HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_BLW_165MHZ             (0x0)
#define HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_BWN_165MHZ_340MHZ      (0x2)
#define HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_ABV_340MHZ             (0x3)

#define HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT          (29)
#define HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_500MV      (0x0) /* for below or equal 3.4 Gbps */
#define HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_300MV      (0x1) /* for above 3.4 Gbps */

#define HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT             (30)
#define HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_10     (0x0) /* for below or equal 3.4 Gbps */
#define HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_40     (0x1) /* for above 3.4 Gbps */

#define HDMI_SRZ_CFG_DATA20BIT10BIT_SHIFT           (31)
#define HDMI_SRZ_CFG_DATA20BIT10BIT_VAL_10_BPC      (0x0)
#define HDMI_SRZ_CFG_DATA20BIT10BIT_VAL_20_BPC      (0x1)

/* ******************************* */
/* register : HDMI_SRZ_PWR_CFG     */
/* ******************************* */

#define HDMI_SRZ_PWR_CFG_EXT_DATACK_EN	BIT(5)
#define HDMI_SRZ_PWR_CFG_EXT_DATACK		BIT(6)
#define HDMI_SRZ_PWR_CFG_PEBYPASS_ENABLE_N	BIT(19)

/* ******************************* */
/* register : HDMI_SRZ_PLL_CFG     */
/* ******************************* */

#define HDMI_PLL_CFG_EN		BIT(0)
#define HDMI_PLL_CFG_NDIV_SHIFT (8)
#define HDMI_PLL_CFG_IDF_SHIFT  (16)
#define HDMI_PLL_CFG_ODF_SHIFT  (24)

#define ODF_DIV_1          (0)
#define ODF_DIV_2          (1)
#define ODF_DIV_4          (2)
#define ODF_DIV_8          (3)
#define ODF_DIV_16         (4)

#define STM_HDMI_NDIV_10   (10)
#define STM_HDMI_NDIV_15   (15)
#define STM_HDMI_NDIV_20   (20)
#define STM_HDMI_NDIV_25   (25)
#define STM_HDMI_NDIV_30   (30)
#define STM_HDMI_NDIV_40   (40)
#define STM_HDMI_NDIV_50   (50)

#define STM_HDMI_MULT_5     (500)
#define STM_HDMI_MULT_6_25  (625)
#define STM_HDMI_MULT_7_5   (750)
#define STM_HDMI_MULT_10    (1000)
#define STM_HDMI_MULT_12_5  (1250)
#define STM_HDMI_MULT_15    (1500)
#define STM_HDMI_MULT_20    (2000)
#define STM_HDMI_MULT_25    (2500)
#define STM_HDMI_MULT_30    (3000)
#define STM_HDMI_MULT_40    (4000)
#define STM_HDMI_MULT_50    (5000)
#define STM_HDMI_MULT_60    (6000)
#define STM_HDMI_MULT_80    (8000)

/* ******************************* */
/* register : HDMI_SRZ_STR_1       */
/* ******************************* */

#define HDMI_SRZ_STR1_MSK_PEXC0   GENMASK(11, 0)
#define HDMI_SRZ_STR1_MSK_PEXC1   GENMASK(27, 16)


/* ******************************* */
/* register : HDMI_SRZ_STR_2       */
/* ******************************* */

#define HDMI_SRZ_STR2_PEXC2       (0)
#define HDMI_SRZ_STR2_MSK_PEXC2   (0xFFF<<HDMI_SRZ_STR2_PEXC2)

/* ******************************* */
/* register : HDMI_SRZ_CALCODE_EXT */
/* ******************************* */

#define HDMI_SRZ_CALCODE_EXT_MASK        GENMASK(27, 0)

/* ******************************** */
/* register : HDMI_SRZ_TX_RSVR_BITS */
/* ******************************** */

#define HDMI_SRZ_TX_RSVR_BITS_BELOW_340MHZ       (0)
#define HDMI_SRZ_TX_RSVR_BITS_ABOVE_340MHZ       (0x0800000)

/* *********************************** */
/* Configuration                       */
/* *********************************** */

/* Config 0 => HDMI_SRZ_CFG */
#define HDMI_SRZ_CONFIG_0_MASK  (HDMI_SRZ_CFG_EN_PE_C0_MASK | \
				 HDMI_SRZ_CFG_EN_PE_C1_MASK | \
				 HDMI_SRZ_CFG_EN_PE_C2_MASK | \
				 HDMI_SRZ_CFG_ISNKCTRL_MASK)
/* Config 1 => HDMI_SRZ_STR_1 */
#define HDMI_SRZ_CONFIG_1_MASK  (HDMI_SRZ_STR1_MSK_PEXC0 | HDMI_SRZ_STR1_MSK_PEXC1)

/* Config 2 => HDMI_SRZ_STR_2 */
#define HDMI_SRZ_CONFIG_2_MASK  (HDMI_SRZ_STR2_MSK_PEXC2)

/* Config 3 => HDMI_SRZ_CALCODE_EXT */
#define HDMI_SRZ_CONFIG_3_MASK  (HDMI_SRZ_CALCODE_EXT_MASK)

#define STM_HDMI_THOLD_CLK_600MHZ  (600000000)
#define STM_HDMI_THOLD_CLK_340MHZ  (340000000)
#define STM_HDMI_THOLD_CLK_165MHZ  (165000000)

#define HDMI_TIMEOUT_PLL_LOCK  50  /*milliseconds */

struct plldividers_s {
	u32 min;
	u32 max;
	u32 idf;
	u32 odf;
};

/*
 * Functional specification recommended values
 */
static struct plldividers_s plldividers[] = {
	{0, 37500000, 1, ODF_DIV_16 },
	{37500000, 75000000, 2, ODF_DIV_8 },
	{75000000, 150000000, 4, ODF_DIV_4 },
	{150000000, 300000000, 8, ODF_DIV_2 },
	{300000000, 600000000, 16, ODF_DIV_1 }
};



static struct hdmi_phy_config hdmiphy_config[] = {
	{0, 145000000, {0x0, 0x0, 0x0, 0x0} },
	{145000000, 165000000, {0x1110, 0x0, 0x0, 0x0} },
	{165000000, 340000000, {0x1110, 0x30003, 0x3, 0x0} },
	{340000000, 600000000, {0x200000, 0x0, 0x0, 0x0} },
};

#if 0
static struct specificplldividers_s {
	u32 px_pllin_clk;
	u32 req_mult;
	u32 idf;
	u32 ndiv;
	u32 odf;
} specificplldividers[] = {
	{13500000,	STM_HDMI_MULT_20,	1,	40,	ODF_DIV_16},
	{13500000,	STM_HDMI_MULT_25,	1,	50,	ODF_DIV_16},
	{13500000,	STM_HDMI_MULT_30,	1,	30,	ODF_DIV_8},
	{13500000,	STM_HDMI_MULT_40,	1,	40,	ODF_DIV_8},
	{13500000,	STM_HDMI_MULT_50,	1,	50,	ODF_DIV_8},
	{13500000,	STM_HDMI_MULT_60,	1,	30,	ODF_DIV_4},
	{13500000,	STM_HDMI_MULT_80,	1,	40,	ODF_DIV_4},
	{16875000,	STM_HDMI_MULT_20,	1,	40,	ODF_DIV_16},
	{16875000,	STM_HDMI_MULT_40,	1,	40,	ODF_DIV_8},
	{20250000,	STM_HDMI_MULT_20,	1,	20,	ODF_DIV_8},
	{20250000,	STM_HDMI_MULT_40,	1,	20,	ODF_DIV_4},
	{21760000,	STM_HDMI_MULT_20,	1,	20,	ODF_DIV_8},
	{25200000,	STM_HDMI_MULT_15,	1,	15,	ODF_DIV_8},
	{25200000,	STM_HDMI_MULT_20,	1,	20,	ODF_DIV_8},
	{27000000,	STM_HDMI_MULT_15,	1,	15,	ODF_DIV_8},
	{27000000,	STM_HDMI_MULT_20,	1,	20,	ODF_DIV_8},
	{27000000,	STM_HDMI_MULT_25,	1,	25,	ODF_DIV_8},
	{27000000,	STM_HDMI_MULT_30,	1,	15,	ODF_DIV_4},
	{27000000,	STM_HDMI_MULT_50,	1,	25,	ODF_DIV_4},
	{27000000,	STM_HDMI_MULT_60,	1,	15,	ODF_DIV_2},
	{27000000,	STM_HDMI_MULT_80,	1,	20,	ODF_DIV_2},
	{33750000,	STM_HDMI_MULT_20,	1,	20,	ODF_DIV_8},
	{40500000,	STM_HDMI_MULT_20,	1,	10,	ODF_DIV_4},
	{40500000,	STM_HDMI_MULT_40,	1,	10,	ODF_DIV_2},
	{54000000,	STM_HDMI_MULT_15,	2,	15,	ODF_DIV_4},
	{54000000,	STM_HDMI_MULT_30,	2,	15,	ODF_DIV_2},
	{59400000,	STM_HDMI_MULT_15,	2,	15,	ODF_DIV_4},
	{72000000,	STM_HDMI_MULT_12_5,	4,	25,	ODF_DIV_4},
	{72000000,	STM_HDMI_MULT_15,	4,	30,	ODF_DIV_4},
	{74250000,	STM_HDMI_MULT_12_5,	4,	25,	ODF_DIV_4},
	{74250000,	STM_HDMI_MULT_15,	4,	30,	ODF_DIV_4},
	{74250000,	STM_HDMI_MULT_20,	4,	40,	ODF_DIV_4},
	{81000000,	STM_HDMI_MULT_20,	2,	10,	ODF_DIV_2},
	{108000000,	STM_HDMI_MULT_15,	4,	15,	ODF_DIV_2},
	{118800000,	STM_HDMI_MULT_15,	4,	15,	ODF_DIV_2},
	{148500000,	STM_HDMI_MULT_12_5,	8,	25,	ODF_DIV_2},
	{148500000,	STM_HDMI_MULT_15,	8,	30,	ODF_DIV_2},
	{148500000,	STM_HDMI_MULT_20,	8,	40,	ODF_DIV_2},
	{148500000,	STM_HDMI_MULT_25,	8,	25,	ODF_DIV_1},
	{148500000,	STM_HDMI_MULT_30,	8,	30,	ODF_DIV_1},
	{148500000,	STM_HDMI_MULT_40,	8,	40,	ODF_DIV_1},
	{150000000,	STM_HDMI_MULT_50,	4,	20,	ODF_DIV_1},
	{297000000,	STM_HDMI_MULT_12_5,	16,	25,	ODF_DIV_1},
	{297000000,	STM_HDMI_MULT_15,	16,	30,	ODF_DIV_1},
	{297000000,	STM_HDMI_MULT_20,	16,	40,	ODF_DIV_1},
	{300000000,	STM_HDMI_MULT_20,	16,	40,	ODF_DIV_1},
	{594000000,	STM_HDMI_MULT_5,	16,	20,	ODF_DIV_2},
	{594000000,	STM_HDMI_MULT_6_25,	16,	25,	ODF_DIV_2},
	{594000000,	STM_HDMI_MULT_7_5,	16,	30,	ODF_DIV_2}
};
#endif


/**
 * sti_hdmi_tx6g0c28phy_start - Start hdmi phy macro cell tx6g0c28
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return false if an error occur
 */
static bool sti_hdmi_tx6g0c28phy_start(struct sti_hdmi *hdmi)
{
	u32 ckpxpll = hdmi->mode.clock * 1000;
	u32 cfg, val, tmdsck, idf, odf, pllctrl = 0;
	bool foundplldivides = false;
	u32 tx_rsvr_bits = HDMI_SRZ_TX_RSVR_BITS_BELOW_340MHZ;
	int i;

	DRM_DEBUG_DRIVER("ckpxpll = %dHz\n", ckpxpll);

	/*
	 * FIXME - in case of output format is STM_VIDEO_OUT_420
	 * ckpxpll is divided by 2 - don't know how to put this
	 * here yet
	 * req_mult_by_100 = STM_HDMI_MULT_10; aka 1000
	 */
	for (i = 0; i < ARRAY_SIZE(plldividers); i++) {
		if (ckpxpll >= plldividers[i].min &&
		    ckpxpll < plldividers[i].max) {
			idf = plldividers[i].idf;
			odf = plldividers[i].odf;
			foundplldivides = true;
			break;
		}
	}

	if (!foundplldivides) {
		DRM_ERROR("input TMDS clock speed (%d) not supported\n",
			  ckpxpll);
		goto err;
	}

	/* Assuming no pixel repetition and 24bits color */
	tmdsck = ckpxpll;
	pllctrl |= 20 << HDMI_PLL_CFG_NDIV_SHIFT;

	if (tmdsck > 600000000) {
		DRM_ERROR("output TMDS clock (%d) out of range\n", tmdsck);
		goto err;
	}

#if 0
	for (i = 0; i < ARRAY_SIZE(specificplldividers); i++) {
		if (ckpxpll >= specificplldividers[i].px_pllin_clk &&
		    req_mult_by_100 == specificplldividers[i].req_mult) {
			printk(KERN_ERR "%s - Line %d - got specificplldividers\n",
			       __func__, __LINE__);
			pllctrl = specificplldividers[i].ndiv << PLL_CFG_NDIV_SHIFT;
			idf = specificplldividers[i].idf;
			odf = specificplldividers[i].odf;
			break;
		}
		if (ckpxpll < specificplldividers[i].px_pllin_clk)
			break;
	}
#endif

	pllctrl |= idf << HDMI_PLL_CFG_IDF_SHIFT;
	pllctrl |= odf << HDMI_PLL_CFG_ODF_SHIFT;

	cfg = (HDMI_SRZ_CFG_EN |
	       HDMI_SRZ_CFG_EXTERNAL_DATA |
	       HDMI_SRZ_CFG_RBIAS_EXT |
	       HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION |
	       (HDMI_SRZ_CFG_DATA20BIT10BIT_VAL_10_BPC << HDMI_SRZ_CFG_DATA20BIT10BIT_SHIFT));

	if (tmdsck > 340000000) {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_ABV_340MHZ << HDMI_SRZ_CFG_EN_SRC_TERMINATION_SHIFT) |
		       (HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_300MV << HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT) |
		       (HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_40 << HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT);
		tx_rsvr_bits = HDMI_SRZ_TX_RSVR_BITS_ABOVE_340MHZ;
	} else if (tmdsck > 165000000) {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_BWN_165MHZ_340MHZ << HDMI_SRZ_CFG_EN_SRC_TERMINATION_SHIFT) |
		       (HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_500MV << HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT) |
		       (HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_10 << HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT);
	} else {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERMINATION_VAL_BLW_165MHZ << HDMI_SRZ_CFG_EN_SRC_TERMINATION_SHIFT) |
		       (HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_500MV << HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT) |
		       (HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_10 << HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT);
	}

	/*
	 * To configure the source termination and pre-emphasis appropriately
	 * for different high speed TMDS clock frequencies a phy configuration
	 * table must be provided, tailored to the SoC and board combination.
	 */
	for (i = 0; i < ARRAY_SIZE(hdmiphy_config); i++) {
		if (hdmiphy_config[i].min_tmds_freq <= tmdsck &&
		    hdmiphy_config[i].max_tmds_freq >= tmdsck) {
			cfg |= (hdmiphy_config[i].config[0] & HDMI_SRZ_CONFIG_0_MASK);
			hdmi_write(hdmi, cfg, HDMI_SRZ_CFG);
			hdmi_write(hdmi, 0, HDMI_SRZ_PWR_CFG);

			val = hdmiphy_config[i].config[1] & HDMI_SRZ_CONFIG_1_MASK;
			hdmi_write(hdmi, val, HDMI_SRZ_STR_1);

			val = hdmiphy_config[i].config[2] & HDMI_SRZ_CONFIG_2_MASK;
			hdmi_write(hdmi, val, HDMI_SRZ_STR_2);

			val = hdmiphy_config[i].config[3] & HDMI_SRZ_CONFIG_3_MASK;
			hdmi_write(hdmi, val, HDMI_SRZ_CALCODE_EXT);

			hdmi_write(hdmi, tx_rsvr_bits, HDMI_SRZ_TX_RSVR_BITS);

			/*
			 * Configure and power up the PHY PLL
			 */
			hdmi->event_received = false;
			hdmi_write(hdmi, (pllctrl | HDMI_PLL_CFG_EN), HDMI_SRZ_PLL_CFG);

			/* wait PLL interrupt */
			wait_event_interruptible_timeout(hdmi->wait_event, hdmi->event_received,
							 msecs_to_jiffies (HDMI_TIMEOUT_PLL_LOCK));

			if ((hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK) == 0) {
				DRM_ERROR("hdmi phy pll not locked\n");
				goto err;
			}

			/* Reset the HDMI_TX_PHY */
			reset_control_assert(hdmi->reset);
			usleep_range(15, 20);
			reset_control_deassert(hdmi->reset);

			return true;
		}
	}

	/*
	 * Default, power up the serializer with no pre-emphasis or
	 * output swing correction
	 */
	hdmi_write(hdmi, cfg, HDMI_SRZ_CFG);
	hdmi_write(hdmi, 0, HDMI_SRZ_PWR_CFG);
	hdmi_write(hdmi, 0, HDMI_SRZ_STR_1);
	hdmi_write(hdmi, 0, HDMI_SRZ_STR_2);
	hdmi_write(hdmi, 0, HDMI_SRZ_CALCODE_EXT);
	hdmi_write(hdmi, 0, HDMI_SRZ_TX_RSVR_BITS);

	return true;

err:
	return false;
}

/**
 * sti_hdmi_tx6g0c28phy_stop - Stop hdmi phy macro cell tx6g0c28
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void sti_hdmi_tx6g0c28phy_stop(struct sti_hdmi *hdmi)
{
	int val = 0;

	DRM_DEBUG_DRIVER("\n");

	hdmi->event_received = false;

	val = HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION;
	hdmi_write(hdmi, val, HDMI_SRZ_CFG);
	hdmi_write(hdmi, 0, HDMI_SRZ_PLL_CFG);

	/* wait PLL interrupt */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_PLL_LOCK));

	if (hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK)
		DRM_ERROR("hdmi phy pll not well disabled\n");
}

struct hdmi_phy_ops tx6g0c28phy_ops = {
	.start = sti_hdmi_tx6g0c28phy_start,
	.stop = sti_hdmi_tx6g0c28phy_stop,
};
