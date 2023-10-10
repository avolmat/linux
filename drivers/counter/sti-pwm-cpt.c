// SPDX-License-IDENTIFIER: GPL-2.0-only

#include <linux/clk.h>
#include <linux/counter.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define CPT_VAL(x) (0x10 + (4 * (x))) /* Capture value */
#define CPT_EDGE(x) (0x30 + (4 * (x))) /* Edge to capture on */

#define CPT_EDGE_MASK      0x03
#define INT_ACK_MASK       0x1ff

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
	struct regmap *regmap;
	struct clk *clk;
	int num_channels;
	u32 snapshot[3];
	unsigned int index;
	struct mutex lock;
	wait_queue_head_t wait;
};

static const struct regmap_config sti_cpt_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const enum counter_synapse_action sti_cpt_synapse_actions[] = {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static struct counter_signal sti_cpt_signals[] = {
	{
		.id = 0,
		.name = "Channel A",
	},
	{
		.id = 1,
		.name = "Channel B",
	}
};

static struct counter_synapse sti_cpt_count_synapses[] = {
	{
		.actions_list = sti_cpt_synapse_actions,
		.num_actions = ARRAY_SIZE(sti_cpt_synapse_actions),
		.signal = &sti_cpt_signals[0]
	},
	{
		.actions_list = sti_cpt_synapse_actions,
		.num_actions = ARRAY_SIZE(sti_cpt_synapse_actions),
		.signal = &sti_cpt_signals[1]
	}
};

static const enum counter_function sti_cpt_functions[] = {
	COUNTER_FUNCTION_INCREASE,
};

static struct counter_count sti_cpt_counts[] = {
	{
		.id = 0,
		.name = "Timer Counter",
		.functions_list = sti_cpt_functions,
		.num_functions = ARRAY_SIZE(sti_cpt_functions),
		.synapses = sti_cpt_count_synapses,
		.num_synapses = ARRAY_SIZE(sti_cpt_count_synapses),
	},
};

static int sti_cpt_count_read(struct counter_device *counter,
			      struct counter_count *count, u64 *value)
{
	return 0;
}

static int sti_cpt_function_read(struct counter_device *counter,
				 struct counter_count *count,
				 enum counter_function *function)
{
	return 0;
}

static int sti_cpt_function_write(struct counter_device *counter,
				  struct counter_count *count,
				  enum counter_function function)
{
	return 0;
}

static int sti_cpt_action_read(struct counter_device *counter,
			       struct counter_count *count,
			       struct counter_synapse *synapse,
			       enum counter_synapse_action *action)
{
	return 0;
}

static int sti_cpt_action_write(struct counter_device *counter,
				struct counter_count *count,
				struct counter_synapse *synapse,
				enum counter_synapse_action action)
{
	return 0;
}

static const struct counter_ops sti_cpt_ops = {
	.count_read     = sti_cpt_count_read,
	.function_read  = sti_cpt_function_read,
	.function_write = sti_cpt_function_write,
	.action_read    = sti_cpt_action_read,
	.action_write   = sti_cpt_action_write
};

static int sti_cpt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct counter_device *counter;
	struct sti_cpt_data *priv;
	void __iomem *mmio;
	int ret;

	counter = devm_counter_alloc(&pdev->dev, sizeof(*priv));
	if (!counter)
		return -ENOMEM;
	priv = counter_priv(counter);

	mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	priv->regmap = devm_regmap_init_mmio(&pdev->dev, mmio,
					     &sti_cpt_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->num_channels = 0;
	ret = of_property_read_u32(np, "st,num-chan", &priv->num_channels);
	if (ret < 0) {
		dev_err(&pdev->dev, "Invalid or missing channel property: %d\n", ret);
		return ret;
	}

	// Handle reg_fields here

	priv->clk = of_clk_get_by_name(np, "cpt");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "Failed to get capture clock\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare(priv->clk);
	if (ret){
		dev_err(&pdev->dev, "Failed to prepare clock: %d\n", ret);
		return ret;
	}

	counter->name = dev_name(&pdev->dev);
	counter->parent = &pdev->dev;
	counter->ops = &sti_cpt_ops;
	counter->counts = sti_cpt_counts;
	counter->num_counts = ARRAY_SIZE(sti_cpt_counts);
	counter->signals = sti_cpt_signals;
	counter->num_signals = ARRAY_SIZE(sti_cpt_signals);

	ret = devm_counter_add(&pdev->dev, counter);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to add device: %d\n", ret);

	return 0;
}

static void sti_cpt_remove(struct platform_device *pdev)
{
	struct counter_device *counter = platform_get_drvdata(pdev);
	struct sti_cpt_data *priv = counter_priv(counter);

	clk_unprepare(priv->clk);
	counter_unregister(counter);
}

static const struct of_device_id sti_cpt_ids[] = {
	{ .compatible = "st,pwm-capture", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sti_cpt_ids);

static struct platform_driver sti_cpt = {
	.driver = {
		.name = "sti-pwm-capture",
		.of_match_table = sti_cpt_ids,
	},
	.probe = sti_cpt_probe,
	.remove_new = sti_cpt_remove,
};
module_platform_driver(sti_cpt);

MODULE_AUTHOR("Raphaël Gallais-Pou <rgallaispou@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics PWM Capture driver");
MODULE_LICENSE("GPL v2");
