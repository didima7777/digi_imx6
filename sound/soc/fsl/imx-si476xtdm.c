/*
 * Copyright (C) 2008-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <sound/soc.h>

#include "imx-audmux.h"

#define DAI_NAME_SIZE	32

struct imx_si476x_data {
	struct snd_soc_dai_link *dai;
	struct snd_soc_card *card;
	unsigned int channels;
};

static int imx_audmux_config(int slave, int master) // int_port-slave ext_port-master
{
	unsigned int ptcr = 0, pdcr = 0;
	slave = slave - 1;
	master = master - 1;

	printk("audmux config master %d -> slave %d \n",master,slave);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN;
	imx_audmux_v2_configure_port(master, ptcr, pdcr);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN |
		IMX_AUDMUX_V2_PTCR_TFSDIR |
		IMX_AUDMUX_V2_PTCR_TFSEL(master) |
		IMX_AUDMUX_V2_PTCR_TCLKDIR |
		IMX_AUDMUX_V2_PTCR_TCSEL(master);
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(master);
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);

	ptcr |=	IMX_AUDMUX_V2_PTCR_RFSDIR |
		IMX_AUDMUX_V2_PTCR_RFSEL(master) |
		IMX_AUDMUX_V2_PTCR_RCLKDIR |
		IMX_AUDMUX_V2_PTCR_RCSEL(master);
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN;
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(slave);
	imx_audmux_v2_configure_port(master, ptcr, pdcr);


	return 0;
}

static int imx_si476x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_si476x_data *priv = snd_soc_card_get_drvdata(rtd->card);

	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	u32 channels_priv;

	u32 channels = params_channels(params);

	if ( priv != NULL ) {
		channels_priv = priv->channels;
		channels = channels_priv;
	} else
		printk(KERN_ALERT "DEBUG: %s, priv is NULL \n",__FUNCTION__);


	//u32 channels = 32;
	//u32 rate = params_rate(params);
	u32 rate = 16000;
	u32 slot_width = 16;
	u32 bclk = rate * channels * slot_width;
	int ret = 0;

	printk(KERN_ALERT "DEBUG: %s, channels_priv = %u \n",__FUNCTION__, channels_priv);

	printk(KERN_ALERT "DEBUG: %s, channels = %u, rate = %u, bclk = %u\n",
		__FUNCTION__, channels, rate, bclk);

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_LEFT_J
			| SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai fmt\n");
		return ret;
	}

	if ( channels == 32 ){
		ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				0xFFFFFFFF, 0xFFFFFFFF,
				channels, slot_width);
	} else {
		ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				(1<<channels) - 1 , (1<<channels) - 1 ,
				channels, slot_width);	
	}
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai tdm slot\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, bclk, SND_SOC_CLOCK_IN);
	if (ret)
		dev_err(cpu_dai->dev, "failed to set sysclk\n");

	return ret;
}

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
			      struct snd_pcm_hw_params *params)
{
	struct imx_si476x_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_interval *rate;
	
	printk("imx_si476x: be_hw_params_fixup %d \n",priv->channels);
	
	//rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	//rate->max = rate->min = priv->channels;

	return 0;
}

static struct snd_soc_ops imx_si476x_ops = {
	.hw_params = imx_si476x_hw_params,
};

static struct snd_soc_dai_link imx_dai = {
	.name = "imx-si476x.0",
	.stream_name = "imx-si476x.0",
	.codec_dai_name = "i2s-codec-dai.0",
	.be_hw_params_fixup = be_hw_params_fixup,
	.ops = &imx_si476x_ops,
};

static struct snd_soc_card snd_soc_card_imx_stack = {
	.name = "imx-audio-si476x",
	.dai_link = &imx_dai,
	.num_links = 1,
	.owner = THIS_MODULE,
};

static int imx_si476x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_stack;
	struct device_node *ssi_np, *np = pdev->dev.of_node;
	struct platform_device *ssi_pdev;
	struct imx_si476x_data *data;
	struct device_node *fm_np = NULL;
	int int_port, ext_port, ret;
	int channels;
	
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto end;
	}
	
	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}


	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}
	
	imx_audmux_config(int_port, ext_port);

	ret = of_property_read_u32(np, "slots", &channels);
	if (ret) {
		dev_err(&pdev->dev, "slots missing or invalid set default value 32\n");
		channels = 32;
	}
	

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	if (!ssi_np) {
		dev_err(&pdev->dev, "ssi_np phandle missing or invalid\n");
		return -EINVAL;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto end;
	}

	fm_np = of_parse_phandle(pdev->dev.of_node, "fm-controller", 0);
	if (!fm_np) {
		dev_err(&pdev->dev, "fm_np phandle missing or invalid\n");
		ret = -EINVAL;
		goto end;
	}

	
	card->dev = &pdev->dev;
	card->dai_link->cpu_dai_name = dev_name(&ssi_pdev->dev);
	card->dai_link->platform_of_node = ssi_np;
	card->dai_link->cpu_of_node = ssi_np; //?
	card->dai_link->codec_of_node = fm_np;

	data->dai = &imx_dai;
	data->card = card;
	data->channels = channels;
	
	platform_set_drvdata(pdev, card);
	
	snd_soc_card_set_drvdata(card,data);

	ret = snd_soc_register_card(card);
	
	if (ret)
		dev_err(&pdev->dev, "Failed to register card: %d\n", ret);

end:
	if (ssi_np)
		of_node_put(ssi_np);
	if (fm_np)
		of_node_put(fm_np);

	return ret;
}

static int imx_si476x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_stack;

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id imx_si476x_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-si476xtdm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_si476x_dt_ids);

static struct platform_driver imx_si476x_driver = {
	.driver = {
		.name = "imx-tuner-si476x",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_si476x_dt_ids,
	},
	.probe = imx_si476x_probe,
	.remove = imx_si476x_remove,
};

module_platform_driver(imx_si476x_driver);

/* Module information */
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("ALSA SoC i.MX si476x");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-tuner-si476x");
