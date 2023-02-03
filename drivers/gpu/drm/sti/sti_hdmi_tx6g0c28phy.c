// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Alain Volmat <avolmat@me.com>
 *
 * This driver is highly inspired from sti_hdmi_tx3g4c28phy.c copyright by ST Microelectronics
 * with IP behavior understood by looking at the display package from 4kopen.com
 * https://bitbucket.org/4kopen/display/src/master/display/ip/hdmi/stmhdmitx6g0_c28_phy.cpp
 */

#include <drm/drm_device.h>
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

#define HDMI_SRZ_CFG_EN_SRC_TERM_SHIFT    (24)
#define HDMI_SRZ_CFG_EN_SRC_TERM_VAL_0_165MHZ             (0x0)
#define HDMI_SRZ_CFG_EN_SRC_TERM_VAL_165_340MHZ      (0x2)
#define HDMI_SRZ_CFG_EN_SRC_TERM_VAL_ABV_340MHZ             (0x3)

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

/* ******************************* */
/* register : HDMI_SRZ_STR_1       */
/* ******************************* */
#define HDMI_SRZ_STR1_MSK_PEXC0   GENMASK(11, 0)
#define HDMI_SRZ_STR1_MSK_PEXC1   GENMASK(27, 16)

/* ******************************* */
/* register : HDMI_SRZ_STR_2       */
/* ******************************* */
#define HDMI_SRZ_STR2_PEXC2       (0)
#define HDMI_SRZ_STR2_MSK_PEXC2   (0xFFF << HDMI_SRZ_STR2_PEXC2)

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

	drm_dbg_driver(hdmi->drm_dev, "%s: ckpxpll = %dHz\n", __func__, ckpxpll);

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
		dev_err(&hdmi->dev, "%s: input TMDS clock speed (%d) not supported\n",
			__func__, ckpxpll);
		return false;
	}

	/* Assuming no pixel repetition and 24bits color */
	tmdsck = ckpxpll;
	pllctrl |= 20 << HDMI_PLL_CFG_NDIV_SHIFT;

	if (tmdsck > STM_HDMI_THOLD_CLK_600MHZ) {
		dev_err(&hdmi->dev, "%s: output TMDS clock (%d) out of range\n", __func__, tmdsck);
		return false;
	}

	pllctrl |= idf << HDMI_PLL_CFG_IDF_SHIFT;
	pllctrl |= odf << HDMI_PLL_CFG_ODF_SHIFT;

	cfg = (HDMI_SRZ_CFG_EN |
	       HDMI_SRZ_CFG_EXTERNAL_DATA |
	       HDMI_SRZ_CFG_RBIAS_EXT |
	       HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION |
	       (HDMI_SRZ_CFG_DATA20BIT10BIT_VAL_10_BPC << HDMI_SRZ_CFG_DATA20BIT10BIT_SHIFT));

	if (tmdsck > STM_HDMI_THOLD_CLK_340MHZ) {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERM_VAL_ABV_340MHZ << HDMI_SRZ_CFG_EN_SRC_TERM_SHIFT) |
		       (HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_300MV << HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT) |
		       (HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_40 << HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT);
		tx_rsvr_bits = HDMI_SRZ_TX_RSVR_BITS_ABOVE_340MHZ;
	} else if (tmdsck > STM_HDMI_THOLD_CLK_165MHZ) {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERM_VAL_165_340MHZ << HDMI_SRZ_CFG_EN_SRC_TERM_SHIFT) |
		       (HDMI_SRZ_CFG_CKCH_LOWSW_EN_VAL_500MV << HDMI_SRZ_CFG_CKCH_LOWSW_EN_SHIFT) |
		       (HDMI_SRZ_CFG_CKBY10_OR_40_VAL_DIV_BY_10 << HDMI_SRZ_CFG_CKBY10_OR_40_SHIFT);
	} else {
		cfg |= (HDMI_SRZ_CFG_EN_SRC_TERM_VAL_0_165MHZ << HDMI_SRZ_CFG_EN_SRC_TERM_SHIFT) |
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
							 msecs_to_jiffies(HDMI_TIMEOUT_PLL_LOCK));

			if ((hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK) == 0) {
				dev_err(&hdmi->dev, "%s: hdmi phy pll not locked\n", __func__);
				return false;
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
}

/**
 * sti_hdmi_tx6g0c28phy_stop - Stop hdmi phy macro cell tx6g0c28
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void sti_hdmi_tx6g0c28phy_stop(struct sti_hdmi *hdmi)
{
	hdmi->event_received = false;

	hdmi_write(hdmi, HDMI_SRZ_CFG_EN_SINK_TERM_DETECTION, HDMI_SRZ_CFG);
	hdmi_write(hdmi, 0, HDMI_SRZ_PLL_CFG);

	/* wait PLL interrupt */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_PLL_LOCK));

	if (hdmi_read(hdmi, HDMI_STA) & HDMI_STA_DLL_LCK)
		dev_err(&hdmi->dev, "%s: hdmi phy pll not well disabled\n", __func__);
}

struct hdmi_phy_ops tx6g0c28phy_ops = {
	.start = sti_hdmi_tx6g0c28phy_start,
	.stop = sti_hdmi_tx6g0c28phy_stop,
};
