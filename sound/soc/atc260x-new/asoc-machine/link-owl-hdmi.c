#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "../sndrv-owl.h"


static int s900_hdmi_link_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	snd_dbg("s900_hdmi_link_init() called\n");

	return 0;
}

static struct snd_soc_dai_link owl_hdmi_dai = {
	.name = "S900 HDMI AUDIO",
	.stream_name = "HDMI PCM",
	.cpu_dai_name = "owl-audio-hdmi",
	.codec_dai_name = "snd-soc-dummy-dai",
	.init = s900_hdmi_link_snd_init,
	.platform_name = "s900-pcm-audio",
	.codec_name = "snd-soc-dummy",
};

static struct snd_soc_card snd_soc_owl_hdmi = {
	.name = "OWL HDMI",
	.owner = THIS_MODULE,
	.dai_link = &owl_hdmi_dai,
	.num_links = 1,
};

static int owl_hdmi_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_owl_hdmi;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		card->dev = NULL;
		return ret;
	}
	return 0;
}

static int owl_hdmi_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	card->dev = NULL;
	return 0;
}

static struct platform_driver owl_hdmi_driver = {
	.driver = {
		.name = "owl-hdmi-audio",
		.owner = THIS_MODULE,
	},
	.probe = owl_hdmi_probe,
	.remove = owl_hdmi_remove,
};

static struct platform_device *owl_hdmi_device;
static int __init owl_hdmi_init(void)
{
	int ret;
	int i = 0;

	printk(KERN_INFO"owl_hdmi_init\n");
	owl_hdmi_device = platform_device_alloc("owl-hdmi-audio", -1);
	if (!owl_hdmi_device) {
		snd_dbg(
				"ASoC: Platform device owl-hdmi-audio allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = platform_device_add(owl_hdmi_device);
	if (ret) {
		snd_dbg(
				"ASoC: Platform device owl-hdmi-audio add failed\n");
		goto err_device_add;
	}

	ret = platform_driver_register(&owl_hdmi_driver);
	if (ret) {
		snd_dbg(
				"ASoC: Platform driver owl-hdmi-audio register failed\n");
		goto err_driver_register;
	}

	return 0;

device_create_file_failed:
err_driver_register:
	platform_device_unregister(owl_hdmi_device);
err_device_add:
	platform_device_put(owl_hdmi_device);
err:
	return ret;
}
static void __exit owl_hdmi_exit(void)
{
	platform_driver_unregister(&owl_hdmi_driver);
	platform_device_unregister(owl_hdmi_device);
	owl_hdmi_device = NULL;
}

module_init(owl_hdmi_init);
module_exit(owl_hdmi_exit);

MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 hdmi PCM module");
MODULE_LICENSE("GPL");

