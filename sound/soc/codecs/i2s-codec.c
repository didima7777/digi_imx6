/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Kristoffer Karlsson <kristoffer.karlsson@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>,
 *         for ST-Ericsson.
 *
 *         Based on the early work done by:
 *         Mikko J. Lehto <mikko.lehto@symbio.com>,
 *         Mikko Sarmanne <mikko.sarmanne@symbio.com>,
 *         Jarmo K. Kuronen <jarmo.kuronen@symbio.com>,
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-sysctrl.h>
#include <linux/mfd/abx500/ab8500-codec.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/clk.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "i2s-codec.h"

/* Macrocell value definitions */
#define CLK_32K_OUT2_DISABLE			0x01
#define INACTIVE_RESET_AUDIO			0x02
#define ENABLE_AUDIO_CLK_TO_AUDIO_BLK		0x10
#define ENABLE_VINTCORE12_SUPPLY		0x04
#define GPIO27_DIR_OUTPUT			0x04
#define GPIO29_DIR_OUTPUT			0x10
#define GPIO31_DIR_OUTPUT			0x40

/* Macrocell register definitions */
#define AB8500_GPIO_DIR4_REG			0x13 /* Bank AB8500_MISC */

/* Nr of FIR/IIR-coeff banks in ANC-block */
#define AB8500_NR_OF_ANC_COEFF_BANKS		2

/* Minimum duration to keep ANC IIR Init bit high or
low before proceeding with the configuration sequence */
#define AB8500_ANC_SM_DELAY			2000

struct filter_control {
	long min, max;
	unsigned int count;
	long value[128];
};

/* Sidetone states */
static const char * const enum_sid_state[] = {
	"Unconfigured",
	"Apply FIR",
	"FIR is configured",
};
enum sid_state {
	SID_UNCONFIGURED = 0,
	SID_APPLY_FIR = 1,
	SID_FIR_CONFIGURED = 2,
};

enum anc_state {
	ANC_UNCONFIGURED = 0,
	ANC_APPLY_FIR_IIR = 1,
	ANC_FIR_IIR_CONFIGURED = 2,
	ANC_APPLY_FIR = 3,
	ANC_FIR_CONFIGURED = 4,
	ANC_APPLY_IIR = 5,
	ANC_IIR_CONFIGURED = 6
};

/* Analog microphones */
enum amic_idx {
	AMIC_IDX_1A,
	AMIC_IDX_1B,
	AMIC_IDX_2
};

struct ab8500_codec_drvdata_dbg {
	struct regulator *vaud;
	struct regulator *vamic1;
	struct regulator *vamic2;
	struct regulator *vdmic;
};

/* Private data for AB8500 device-driver */
struct ab8500_codec_drvdata {
	struct regmap *regmap;
	struct mutex ctrl_lock;
};

/*
 * Read'n'write functions
 */

/* Read a register from the audio-bank of AB8500 */
static int ab8500_codec_read_reg(void *context, unsigned int reg,
				 unsigned int *value)
{

	*value = 0;
	return 0;
}

/* Write to a register in the audio-bank of AB8500 */
static int ab8500_codec_write_reg(void *context, unsigned int reg,
				  unsigned int value)
{
	return 0;
}

static const struct regmap_config ab8500_codec_regmap = {
	.reg_read = ab8500_codec_read_reg,
	.reg_write = ab8500_codec_write_reg,
};


/*
 * DAPM-widgets
 */

static const struct snd_soc_dapm_widget ab8500_dapm_widgets[] = {

};

/*
 * DAPM-routes
 */
static const struct snd_soc_dapm_route ab8500_dapm_routes[] = {
};


static struct snd_kcontrol_new ab8500_ctrls[] = {
};

static int ab8500_codec_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int status;

	dev_dbg(codec->dev, "%s: Enter (fmt = 0x%x)\n", __func__, fmt);


	return 0;
}

static int ab8500_codec_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{

	return 0;
}

static const struct snd_soc_dai_ops ab8500_codec_ops = {
	.set_fmt = ab8500_codec_set_dai_fmt,
	.set_tdm_slot = ab8500_codec_set_dai_tdm_slot,
};

static struct snd_soc_dai_driver ab8500_codec_dai[] = {
	{
		.name = "i2s-codec-dai.0",
		.id = 0,
		.playback = {
			.stream_name = "playback",
			.channels_min = 1,
			.channels_max = 32,
			.rates = AB8500_SUPPORTED_RATE,
			.formats = AB8500_SUPPORTED_FMT,
		},
		.capture = {
			.stream_name = "capture1",
			.channels_min =  1,
			.channels_max = 32,
			.rates = AB8500_SUPPORTED_RATE,
			.formats = AB8500_SUPPORTED_FMT,
		},
		.ops = &ab8500_codec_ops,
		.symmetric_rates = 1
	},
	{
		.name = "i2s-codec-dai.1",
		.id = 1,
		.capture = {
			.stream_name = "capture2",
			.channels_min = 1,
			.channels_max = 32,
			.rates = AB8500_SUPPORTED_RATE,
			.formats = AB8500_SUPPORTED_FMT,
		},
		.ops = &ab8500_codec_ops,
		.symmetric_rates = 1
	}
};

static int ab8500_codec_probe(struct snd_soc_codec *codec)
{
	int status =0;
	printk("TDM driver \n");
	return status;
};

static struct snd_soc_codec_driver ab8500_codec_driver = {
	.probe =		ab8500_codec_probe,
	.component_driver = {
		.controls =		ab8500_ctrls,
		.num_controls =		ARRAY_SIZE(ab8500_ctrls),
		.dapm_widgets =		ab8500_dapm_widgets,
		.num_dapm_widgets =	ARRAY_SIZE(ab8500_dapm_widgets),
		.dapm_routes =		ab8500_dapm_routes,
		.num_dapm_routes =	ARRAY_SIZE(ab8500_dapm_routes),
	},
};

static int ab8500_codec_driver_probe(struct platform_device *pdev)
{
	int status, err;
	struct ab8500_codec_drvdata *drvdata;
	struct clk *codec_clk;

	dev_dbg(&pdev->dev, "%s: Enter.\n", __func__);

	/* Create driver private-data struct */
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct ab8500_codec_drvdata),
			GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	
	dev_set_drvdata(&pdev->dev, drvdata);

	drvdata->regmap = devm_regmap_init(&pdev->dev, NULL, &pdev->dev,
					   &ab8500_codec_regmap);
					   
	if (IS_ERR(drvdata->regmap)) {
		status = PTR_ERR(drvdata->regmap);
		dev_err(&pdev->dev, "%s: Failed to allocate regmap: %d\n",
			__func__, status);
		return status;
	}

	codec_clk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(codec_clk)) {
		dev_err(&pdev->dev, "get mclk failed\n");
		err = PTR_ERR(codec_clk);
		status = -1;
		goto err_exit;
	}
	clk_prepare_enable(codec_clk);

	dev_dbg(&pdev->dev, "%s: Register codec.\n", __func__);
	status = snd_soc_register_codec(&pdev->dev, &ab8500_codec_driver,
				ab8500_codec_dai,
				ARRAY_SIZE(ab8500_codec_dai));

err_exit:
	if (status < 0)
		dev_err(&pdev->dev,
			"%s: Error: Failed to register codec (%d).\n",
			__func__, status);
	return status;
}

static int ab8500_codec_driver_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s Enter.\n", __func__);

	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id ab8500_dt_ids[] = {
	{ .compatible = "ti,i2scodec", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ab8500_dt_ids);

static struct platform_driver ab8500_codec_platform_driver = {
	.driver	= {
		.name	= "ab8500-codec",
		.of_match_table = ab8500_dt_ids,
	},
	.probe		= ab8500_codec_driver_probe,
	.remove		= ab8500_codec_driver_remove,
	.suspend	= NULL,
	.resume		= NULL,
};
module_platform_driver(ab8500_codec_platform_driver);

MODULE_LICENSE("GPL v2");
