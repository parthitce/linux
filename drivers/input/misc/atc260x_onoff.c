#include <linux/irq.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/atc260x/atc260x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static irqreturn_t atc260x_onoff_irq(int irq, void *data)
{
	struct atc260x_dev *atc260x = data;
	unsigned int onoff_time = 2;
	int ret;

	int val, val2, pd;
//	regmap_update_bits(atc260x->regmap, ATC2603C_INTS_MSK, BIT(6), 0);
	regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(12), 0);
	regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(2), 0);
	ret = regmap_read(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, &val);
	ret |= regmap_read(atc260x->regmap, ATC2603C_PMU_SYS_CTL1, &val2);
	ret |= regmap_read(atc260x->regmap, ATC2603C_INTS_PD, &pd);
	pr_info("%s called %d ctl2 %x ctl1 %x pd %x\n", __func__, ret, val, val2, pd);

	ssleep(1);
	ret = regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(14), 1);
	ret |= regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(13), 1);
	ret |= regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(2), 1);

	ssleep(1);
	ret |= regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(12), 1);
	ret |= regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, BIT(2), 1);
	ret |= regmap_read(atc260x->regmap, ATC2603C_PMU_SYS_CTL2, &val);
	ret |= regmap_read(atc260x->regmap, ATC2603C_PMU_SYS_CTL1, &val2);
	ret |= regmap_read(atc260x->regmap, ATC2603C_INTS_PD, &pd);
	pr_info("%s called %d ctl2 %x ctl1 %x pd %x\n", __func__, ret, val, val2, pd);
//	regmap_update_bits(atc260x->regmap, ATC2603C_INTS_MSK, BIT(6), 1);

        return IRQ_HANDLED;
}

static int atc260x_onoff_probe(struct platform_device *pdev)
{
	const struct platform_device_id *match = platform_get_device_id(pdev);
	struct atc260x_dev *atc260x;
	int irq_onoff;
	int ret;

	if (!match) {
		dev_err(&pdev->dev, "Failed to get platform_device_id\n");
		return -EINVAL;
	}

	atc260x = dev_get_drvdata(pdev->dev.parent);

	unsigned int onoff_time = 2;
	regmap_update_bits(atc260x->regmap, ATC2603C_PMU_SYS_CTL2,
				(1U << 12) | (1 << 1) | (3U << 10) | (1 << 14) | (1 << 13) | (1 << 2),
				(1U << 12) | (1 << 1) | (onoff_time << 10) | (1 << 14) | (1 << 13) | (1 << 2));

	irq_onoff = platform_get_irq_byname(pdev, "ATC260C_ONOFF");
	if (irq_onoff < 0) {
		dev_err(&pdev->dev, "No IRQ for PEK_DBR, error=%d\n", irq_onoff);
		return irq_onoff;
	}
	irq_onoff = regmap_irq_get_virq(atc260x->regmap_irqc, irq_onoff);

	pr_info("virq onoff: %d\n", irq_onoff);

	ret = request_threaded_irq(irq_onoff, NULL, atc260x_onoff_irq,
			IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"atc260x_onoff", atc260x);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to request IRQ: %d\n", ret);
		return -1;
	}

	pr_info("PMIC ONOFF pin irq success\n");
	return 0;
}

static const struct platform_device_id atc260x_onoff_id_match[] = {
        {
                .name = "atc2603c-onoff",
        },
        { /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, atc260x_onoff_id_match);

static struct platform_driver atc260x_onoff_driver = {
        .probe          = atc260x_onoff_probe,
        .id_table       = atc260x_onoff_id_match,
        .driver         = {
                .name           = "atc260x-onoff",
        },
};
module_platform_driver(atc260x_onoff_driver);

MODULE_DESCRIPTION("atc260x input button driver");
MODULE_AUTHOR("Parthiban Nallathambi <pn@denx.de>");
MODULE_LICENSE("GPL");
