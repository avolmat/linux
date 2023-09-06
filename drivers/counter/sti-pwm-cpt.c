// SPDX-License-IDENTIFIER: GPL-2.0-only

#include <linux/clk.h>
#include <linux/counter.h>
#include <linux/of.h>
#include <platform_device.h>

#define PWM_CPT_VAL(x) (0x10 + (4 * (x))) /* Capture value */
#define PWM_CPT_EDGE(x) (0x30 + (4 * (x))) /* Edge to capture on */

#define PWM_CPT_EDGE_MASK      0x03
#define PWM_INT_ACK_MASK       0x1ff

#define STI_MAX_CPT_DEVS       4
#define CPT_DC_MAX             0xff

/*
 * Each capture input can be programmed to detect rising-edge, falling-edge,
 * either edge or neither egde.
 */
enum sti_cpt_edge {
       CPT_EDGE_DISABLED,
       CPT_EDGE_RISING,
       CPT_EDGE_FALLING,
       CPT_EDGE_BOTH,
};

struct sti_cpt_data {
       u32 snapshot[3];
       unsigned int index;
       struct mutex lock;
       wait_queue_head_t wait;
};

static int sti_cpt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct counter_device *counter;
	struct spi_cpt_data *priv;

	counter = devm_counter_alloc(&pdev->dev, sizeof(*priv));
	priv = counter_priv(counter);

	regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return 0;
}

static const struct of_device_id sti_cpt_ids[] = {
	{ compatible = "st,pwm-capture", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sti_cpt_ids);

static struct platform_driver sti_cpt = {
	.probe = sti_cpt_probe,
	.driver = {
		.name = "sti-pwm-capture",
		.of_match_table = sti_cpt_ids,
	},
};
module_platform_driver(sti_cpt);

MODULE_AUTHOR("Raphaël Gallais-Pou <rgallaispou@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics PWM Capture driver");
MODULE_LICENSE("GPL v2");
