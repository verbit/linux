/*
 * ASoC Driver for AudioInjector Pi add on soundcard
 *
 *  Created on: 13-May-2016
 *      Author: flatmax@flatmax.org
 *              based on code by  Cliff Cai <Cliff.Cai@analog.com> for the ssm2602 machine blackfin.
 *              with help from Lars-Peter Clausen for simplifying the original code to use the dai_fmt field.
 *		i2s_node code taken from the other sound/soc/bcm machine drivers.
 *
 * Copyright (C) 2016 Flatmax Pty. Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/types.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/control.h>

#include "../codecs/wm8731.h"
#include "../codecs/wm8804.h"

static short int auto_shutdown_output;
module_param(auto_shutdown_output, short,
	      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(auto_shutdown_output, "Shutdown SP/DIF output if playback is stopped");


static const unsigned int bcm2835_rates_12000000[] = {
	8000, 16000, 32000, 44100, 48000, 96000, 88200,
};

static struct snd_pcm_hw_constraint_list bcm2835_constraints_12000000 = {
	.list = bcm2835_rates_12000000,
	.count = ARRAY_SIZE(bcm2835_rates_12000000),
};

static int snd_verbit_helium_soundcard_dac_startup(struct snd_pcm_substream *substream)
{
	/* Setup constraints, because there is a 12 MHz XTAL on the board */
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&bcm2835_constraints_12000000);
	return 0;
}

static int snd_verbit_helium_soundcard_dac_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	switch (params_rate(params)){
		case 8000:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 1);
		case 16000:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 750);
		case 32000:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 375);
		case 44100:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 272);
		case 48000:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 250);
		case 88200:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 136);
		case 96000:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 125);
		default:
			return snd_soc_dai_set_bclk_ratio(cpu_dai, 125);
	}
}

/* machine stream operations */
static struct snd_soc_ops snd_verbit_helium_soundcard_dac_ops = {
	.startup = snd_verbit_helium_soundcard_dac_startup,
	.hw_params = snd_verbit_helium_soundcard_dac_hw_params,
};

static int verbit_helium_soundcard_dac_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	return snd_soc_dai_set_sysclk(rtd->codec_dai, WM8731_SYSCLK_XTAL, 12000000, SND_SOC_CLOCK_IN);
}


static int snd_verbit_helium_soundcard_spdif_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	int sysclk = 12000000; /* This is fixed on this board */

	long mclk_freq = 0;
	int mclk_div = 1;
	int sampling_freq = 1;

	int ret;

	int samplerate = params_rate(params);

	if (samplerate <= 96000) {
		mclk_freq = samplerate * 256;
		mclk_div = WM8804_MCLKDIV_256FS;
	} else {
		mclk_freq = samplerate * 128;
		mclk_div = WM8804_MCLKDIV_128FS;
	}

	switch (samplerate) {
	case 32000:
		sampling_freq = 0x03;
		break;
	case 44100:
		sampling_freq = 0x00;
		break;
	case 48000:
		sampling_freq = 0x02;
		break;
	case 88200:
		sampling_freq = 0x08;
		break;
	case 96000:
		sampling_freq = 0x0a;
		break;
	case 176400:
		sampling_freq = 0x0c;
		break;
	case 192000:
		sampling_freq = 0x0e;
		break;
	default:
		dev_err(codec->dev, "Failed to set WM8804 SYSCLK, unsupported samplerate %d\n",
			samplerate);
	}

	snd_soc_dai_set_clkdiv(codec_dai, WM8804_MCLK_DIV, mclk_div);
	snd_soc_dai_set_pll(codec_dai, 0, 0, sysclk, mclk_freq);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8804_TX_CLKSRC_PLL,
					sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set WM8804 SYSCLK: %d\n", ret);
		return ret;
	}

	/* Enable TX output */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);

	/* Power on */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x9, 0);

	/* set sampling frequency status bits */
	snd_soc_update_bits(codec, WM8804_SPDTX4, 0x0f, sampling_freq);

	return snd_soc_dai_set_bclk_ratio(cpu_dai, 64);
}

static int snd_verbit_helium_soundcard_spdif_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	/* turn on digital output */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x3c, 0x00);

	return 0;
}

static void snd_verbit_helium_soundcard_spdif_shutdown(struct snd_pcm_substream *substream)
{
	if (auto_shutdown_output) {
		struct snd_soc_pcm_runtime *rtd = substream->private_data;
		struct snd_soc_codec *codec = rtd->codec;

		/* turn off digital output */
		snd_soc_update_bits(codec, WM8804_PWRDN, 0x3c, 0x3c);
	}
}

static struct snd_soc_ops snd_verbit_helium_soundcard_spdif_ops = {
	.hw_params = snd_verbit_helium_soundcard_spdif_hw_params,
	.startup = snd_verbit_helium_soundcard_spdif_startup,
	.shutdown = snd_verbit_helium_soundcard_spdif_shutdown,

};

static int verbit_helium_soundcard_spdif_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	/* enable TX output */
	snd_soc_update_bits(codec, WM8804_PWRDN, 0x4, 0x0);
	/* set all input to CMOS */
	snd_soc_update_bits(codec, WM8804_SPDMODE, 0xFF, 0x00);

	return 0;
}

static struct snd_soc_dai_link verbit_helium_soundcard_dai[] = {
	{
		.name = "verbit helium DAC",
		.stream_name = "verbit helium DAC",
		.cpu_dai_name	= "bcm2835-i2s",
		.codec_dai_name = "wm8731-hifi",
		.platform_name	= "20203000.i2s",
		.codec_name = "wm8731.1-001a",
		.ops = &snd_verbit_helium_soundcard_dac_ops,
		.init = verbit_helium_soundcard_dac_dai_init,
		.dai_fmt = SND_SOC_DAIFMT_CBM_CFM|SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF,
	},
	{
		.name = "verbit helium SPDIF",
		.stream_name = "verbit helium SPDIF",
		.cpu_dai_name	= "bcm2835-i2s",
		.codec_dai_name = "wm8804-spdif",
		.platform_name	= "20203000.i2s",
		.codec_name = "wm8804.1-003b",
		.ops = &snd_verbit_helium_soundcard_spdif_ops,
		.init = verbit_helium_soundcard_spdif_dai_init,
		.dai_fmt = SND_SOC_DAIFMT_CBM_CFM|SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF,

	},
	/*{
		.name = "verbit helium SPDIF-DAC",
		.stream_name = "verbit helium SPDIF-DAC",
		.cpu_dai_name	= "wm8731-hifi",
		.codec_dai_name = "wm8804-spdif",
		.codec_name = "wm8804.1-003b",
		//.ops = &snd_verbit_helium_soundcard_ops,
		//.init = verbit_helium_soundcard_dai_init,
		.dai_fmt = SND_SOC_DAIFMT_CBM_CFM|SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF,

	},*/
};

static const struct snd_soc_dapm_widget wm8731_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_LINE("Line In Jacks", NULL),
	SND_SOC_DAPM_MIC("Microphone", NULL),
	//SND_SOC_DAPM_INPUT("Optical In"),
};

static const struct snd_soc_dapm_route audioinjector_audio_map[] = {
	/* headphone connected to LHPOUT, RHPOUT */
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	/* speaker connected to LOUT, ROUT */
	{"Ext Spk", NULL, "ROUT"},
	{"Ext Spk", NULL, "LOUT"},

	/* line inputs */
	{"Line In Jacks", NULL, "Line Input"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"Microphone", NULL, "Mic Bias"},

	/* optical */
	//{"Optical In", NULL, "SPDIF In"},
};

static struct snd_soc_card snd_soc_audioinjector = {
	.name = "verbit helium soundcard",
	.dai_link = verbit_helium_soundcard_dai,
	.num_links = ARRAY_SIZE(verbit_helium_soundcard_dai),

	.dapm_widgets = wm8731_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8731_dapm_widgets),
	.dapm_routes = audioinjector_audio_map,
	.num_dapm_routes = ARRAY_SIZE(audioinjector_audio_map),
};

static int verbit_helium_soundcard_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_audioinjector;
	int ret;
	
	card->dev = &pdev->dev;

	if (pdev->dev.of_node) {
		struct snd_soc_dai_link *dai = &verbit_helium_soundcard_dai[0];
		struct device_node *i2s_node = of_parse_phandle(pdev->dev.of_node,
								"i2s-controller", 0);

		if (i2s_node) {
			dai->cpu_dai_name = NULL;
			dai->cpu_of_node = i2s_node;
			dai->platform_name = NULL;
			dai->platform_of_node = i2s_node;
		} else
			if (!dai->cpu_of_node) {
				dev_err(&pdev->dev, "Property 'i2s-controller' missing or invalid\n");
				return -EINVAL;
			}
	}

	if ((ret = snd_soc_register_card(card))) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
	}
	return ret;
}

static int verbit_helium_soundcard_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	return snd_soc_unregister_card(card);

}

static const struct of_device_id verbit_helium_soundcard_of_match[] = {
	{ .compatible = "verbit,helium", },
	{},
};
MODULE_DEVICE_TABLE(of, verbit_helium_soundcard_of_match);

static struct platform_driver verbit_helium_soundcard_driver = {
       .driver         = {
		.name   = "verbit-helium-stereo",
		.owner  = THIS_MODULE,
		.of_match_table = verbit_helium_soundcard_of_match,
       },
       .probe          = verbit_helium_soundcard_probe,
       .remove         = verbit_helium_soundcard_remove,
};

module_platform_driver(verbit_helium_soundcard_driver);
MODULE_AUTHOR("Ilya Verbitskiy <ilya@verbit.io>");
MODULE_DESCRIPTION("verbit helium soundcard");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:verbit-helium-soundcard");

