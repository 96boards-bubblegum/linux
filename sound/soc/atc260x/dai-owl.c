#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <linux/clk.h>			/* clk_enable */
//#include <mach/clkname.h>
#include <linux/cpufreq.h>
//#include <mach/hardware.h>
#include <linux/io.h> 
#include <linux/ioport.h>
#include "sndrv-owl.h"
#include "common-regs-owl.h"
//#include <mach/module-owl.h>

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>

static int dai_clk_i2s_count;
static int dai_clk_hdmi_count;
static int dai_clk_spdif_count;
static int dai_mode_i2s_count;
static int dai_mode_hdmi_count;

static int dai_clk_enabled;

struct asoc_dai_resource {
    void __iomem    *base[MAX_RES_NUM];/*virtual base for every resource*/
    void __iomem    *baseptr; /*pointer to every virtual base*/
    struct clk      *i2stx_clk;
    struct clk	    *i2srx_clk;
    struct clk	    *hdmia_clk;
    struct clk      *spdif_clk;
    int             irq;
    unsigned int    setting;
    struct dma_chan* tx_chan;
    struct dma_chan* rx_chan;
    struct dma_chan* hdmia_chan;
};

//dai resources
static struct asoc_dai_resource dai_res;

extern int atc2609a_audio_prepare_and_set_clk(int rate);
extern int atc2609a_audio_disable_clk();

extern int atc2609a_audio_hdmia_set_clk(int rate);


void __iomem* get_dai_reg_base(int num)
{
	return dai_res.base[num];
}
EXPORT_SYMBOL_GPL(get_dai_reg_base);

void set_dai_reg_base(int num)
{
	dai_res.baseptr = dai_res.base[num];
}

EXPORT_SYMBOL_GPL(set_dai_reg_base);

u32 snd_dai_readl(u32 reg)
{
	u32 res;
	res = readl(dai_res.baseptr + reg);
	return res;
}

EXPORT_SYMBOL_GPL(snd_dai_readl);
	 
void snd_dai_writel(u32 val, u32 reg)
{
	u32 reg_val;
	writel(val, dai_res.baseptr + reg);
	
	reg_val = readl(dai_res.baseptr + reg);
	//printk("dai[0x%x]: 0x%x after 0x%x\n", reg, reg_val, val);
	
}

void snd_dai_i2s_clk_disable(void)
{
	if(dai_clk_enabled == 1)
	{
		clk_disable(dai_res.i2srx_clk);
		clk_disable(dai_res.i2stx_clk);	
		dai_clk_enabled = 0;
	}
}
EXPORT_SYMBOL_GPL(snd_dai_i2s_clk_disable);

void snd_dai_i2s_clk_enable(void)
{
	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);	
	dai_clk_enabled = 1;
}
EXPORT_SYMBOL_GPL(snd_dai_i2s_clk_enable);


EXPORT_SYMBOL_GPL(snd_dai_writel);



static ssize_t error_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", error_switch);
	return cnt;
}

static ssize_t error_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		error_switch = tmp;
		break;
	default:
		printk(KERN_ERR"invalid input\n");
		break;
	}
	return count;
}

static ssize_t debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cnt;

	cnt = sprintf(buf, "%d\n(Note: 1: open, 0:close)\n", debug_switch);
	return cnt;
}

static ssize_t debug_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int cnt, tmp;
	cnt = sscanf(buf, "%d", &tmp);
	switch (tmp) {
	case 0:
	case 1:
		debug_switch = tmp;
		break;
	default:
		printk(KERN_INFO"invalid input\n");
		break;
	}
	return count;
}

static struct device_attribute dai_attr[] = {
	__ATTR(error, S_IRUSR | S_IWUSR, error_show, error_store),
	__ATTR(debug, S_IRUSR | S_IWUSR, debug_show, debug_store),
};

/******************************************************************************/
/*!
 * \par  Description:
 *    将采样率转换成矊 *¬件寄存器设置所需的索引值
 * \param[in]    sample_rate  采样率
 * \param[in]    mode 输入输出模
 *式 （如果是spdif或者hdmi，index不同）
 * \return       索引值
 * \retval           -1 failed
 * \ingroup      sndrv
 *****************************************/
static int get_sf_index(int sample_rate, int mode)
{
	int i = 0;
	char fs = sample_rate / 1000;
	// 44k 在基础上寄存器bit位基础上加16 
	static fs_t fs_list[] = {
		{ 384, { -1, 0} },
		{ 352, { -1, 16} },
		{ 192, { 0,  1} },
		{ 176, { 16, 17} },
		{ 96,  { 1,  3} },
		{ 88,  { 17, 19} },
		{ 64,  { 2, -1} },
		{ 48,  { 3,  5} },
		{ 44,  { 19, 21} },
		{ 32,  { 4,  6} },
		{ 24,  { 5, -1} },
		{ 22,  {21, -1} },
		{ 16,  { 6, -1} },
		{ 12,  { 7, -1} },
		{ 11,  {23, -1} },
		{ 8,   { 8, -1} },
		{ -1,  {-1, -1} }
	};

	while ((fs_list[i].sample_rate > 0) && (fs_list[i].sample_rate != fs))
		i++;

	if ((mode == O_MODE_HDMI) || (mode == O_MODE_SPDIF))
		return fs_list[i].index[1];
	else
		return fs_list[i].index[0];
}



static int s900_dai_record_clk_set(int mode, int rate)
{
	struct clk *apll_clk;
	unsigned long reg_val;
	int sf_index, ret;

	if (dai_clk_i2s_count > 0) {
		dai_clk_i2s_count++;
		return 0;
	}
//	module_clk_enable(MOD_ID_I2SRX);
//	module_clk_enable(MOD_ID_I2STX);
	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);
	dai_clk_enabled = 1;


	sf_index = get_sf_index(rate, mode);
	if (sf_index & 0x10)
		reg_val = 45158400;
	else
		reg_val = 49152000;

//	apll_clk = clk_get(NULL, CLKNAME_AUDIOPLL);
//	clk_prepare(apll_clk);
//	ret = clk_set_rate(apll_clk, reg_val);
	ret = atc2609a_audio_prepare_and_set_clk(reg_val);
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}

	//apll_clk = clk_get(NULL, CLKNAME_I2STX_CLK);
	ret = clk_set_rate(dai_res.i2stx_clk, rate << 8);
	if (ret) {
		snd_err("i2stx clk rate set error!!\n");
		return ret;
	}
	//apll_clk = clk_get(NULL, CLKNAME_I2SRX_CLK);
	ret = clk_set_rate(dai_res.i2srx_clk, rate << 8);
	if (ret) {
		snd_err("i2srx clk rate set error!!\n");
		return ret;
	}
	dai_clk_i2s_count++;

	return 0;
}

static int s900_dai_clk_set(int mode, int rate)
{
	//struct clk *apll_clk;
	unsigned long reg_val;
	int sf_index, ret;

//	module_clk_enable(MOD_ID_I2SRX);
//	module_clk_enable(MOD_ID_I2STX);
	clk_prepare_enable(dai_res.i2srx_clk);
	clk_prepare_enable(dai_res.i2stx_clk);
	dai_clk_enabled = 1;
	
	sf_index = get_sf_index(rate, mode);
	if (sf_index & 0x10)
		reg_val = 45158400;
	else
		reg_val = 49152000;

	
//	apll_clk = clk_get(NULL, CLKNAME_AUDIOPLL);
//	clk_prepare(apll_clk);
//	ret = clk_set_rate(apll_clk, reg_val);
	ret = atc2609a_audio_prepare_and_set_clk(reg_val);
	if (ret < 0) {
		snd_err("audiopll set error!\n");
		return ret;
	}

	switch (mode) {
	case O_MODE_I2S:
	if (dai_clk_i2s_count == 0) {
		//printk("%s %d: %d\n", __FUNCTION__, __LINE__, rate);
		//apll_clk = clk_get(NULL, CLKNAME_I2STX_CLK);
		ret = clk_set_rate(dai_res.i2stx_clk, rate << 8);
		if (ret) {
			snd_dbg("i2stx clk rate set error!!\n");
			return ret;
		}
		//apll_clk = clk_get(NULL, CLKNAME_I2SRX_CLK);
		ret = clk_set_rate(dai_res.i2srx_clk, rate << 8);
		if (ret) {
			snd_dbg("i2srx clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_i2s_count++;
	break;

	case O_MODE_HDMI:
	if (dai_clk_hdmi_count == 0) {
		//apll_clk = clk_get(NULL, CLKNAME_HDMIA_CLK);
		ret = atc2609a_audio_hdmia_set_clk(rate << 7);
		//ret = clk_set_rate(dai_res.hdmia_clk, rate << 7);
		if (ret) {
			snd_dbg("hdmi clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_hdmi_count++;
	break;

	case O_MODE_SPDIF:
	if (dai_clk_spdif_count == 0) {
		//apll_clk = clk_get(NULL, CLKNAME_SPDIF_CLK);
		ret = clk_set_rate(dai_res.spdif_clk, rate << 7);
		if (ret) {
			snd_dbg("spdif clk rate set error!!\n");
			return ret;
		}
	}
	dai_clk_spdif_count++;
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s900_dai_record_clk_disable(void)
{
	//atc2609a_audio_disable_clk();
	
	//clk_disable(dai_res.i2srx_clk);
	//clk_disable(dai_res.i2stx_clk);	
	
	if (dai_clk_i2s_count > 0)
		dai_clk_i2s_count--;

	return 0;
}

static int s900_dai_clk_disable(int mode)
{
	switch (mode) {
	case O_MODE_I2S:
	/* we disable the i2s_clk in another place */
	/*
	apll_clk = clk_get_sys(CLK_NAME_I2STX_CLK, NULL);
	clk_disable(apll_clk);
	apll_clk = clk_get_sys(CLK_NAME_I2SRX_CLK, NULL);
	clk_disable(apll_clk);
	*/
	printk("%s %d disable dai\n", __FUNCTION__, __LINE__);
	//clk_disable(dai_res.i2srx_clk);
	//clk_disable(dai_res.i2stx_clk);	
	//atc2609a_audio_disable_clk();

	if (dai_clk_i2s_count > 0)
		dai_clk_i2s_count--;
	break;

	case O_MODE_HDMI:
	if (dai_clk_hdmi_count > 0)
		dai_clk_hdmi_count--;
	break;

	case O_MODE_SPDIF:
	if (dai_clk_spdif_count > 0)
		dai_clk_spdif_count--;
	break;
	default:
		return -EINVAL;
	}
	return 0;
}

#if 0
static int s900_i2s_4wire_config(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	int ret;

	pcm_priv->pc = pinctrl_get(dai->dev);
	if (IS_ERR(pcm_priv->pc) || (pcm_priv->pc == NULL)) {
		snd_dbg("i2s pin control failed!\n");
		return -EAGAIN;
	}

	pcm_priv->ps = pinctrl_lookup_state(pcm_priv->pc, "default");
	if (IS_ERR(pcm_priv->ps) || (pcm_priv->ps == NULL)) {
		snd_dbg("i2s pin state get failed!\n");
		return -EAGAIN;
	}

	ret = pinctrl_select_state(pcm_priv->pc, pcm_priv->ps);
	if (ret) {
		snd_dbg("i2s pin state set failed!\n");
		return -EAGAIN;
	}

	/*
	 * set 4wire mode
	snd_dai_writel(snd_dai_readl(PAD_CTL) | (0x1 << 1), PAD_CTL);
	snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 2), MFP_CTL0);
	snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 5), MFP_CTL0);
	 */

	/* disable i2s tx&rx */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

	/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
	snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
	snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) | (0x3 << 9) | 0x3, I2S_FIFOCTL);

	/* this should before enable rx/tx,
	or after suspend, data may be corrupt */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
	snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
	/* set i2s mode I2S_RX_ClkSel==1 */
	snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

	/* enable i2s rx/tx at the same time */
	snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

	/* i2s rx 00: 2.0-Channel Mode */
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
	snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);

	return 0;
}
#endif

static int s900_dai_record_mode_set(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	/*ret = s900_i2s_4wire_config(pcm_priv, dai);*/
	/*snd_dai_writel(snd_dai_readl(PAD_CTL) | (0x1 << 1), PAD_CTL);*/
	if (dai_mode_i2s_count == 0) {
//		set_dai_reg_base(GPIO_MFP_NUM);
//		snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 2), MFP_CTL0);
//		snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x3 << 3), MFP_CTL0);

		/* disable i2s tx&rx */
		set_dai_reg_base(I2S_SPDIF_NUM);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

		/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
		& ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
			| (0x3 << 9) | 0x3, I2S_FIFOCTL);

		/* this should before enable rx/tx,
		or after suspend, data may be corrupt */
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
		/* set i2s mode I2S_RX_ClkSel==1 */
		snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

		/* enable i2s rx/tx at the same time */
		snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

		/* i2s rx 00: 2.0-Channel Mode */
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);
	}
	dai_mode_i2s_count++;

	return 0;
}


static int s900_dai_mode_set(struct s900_pcm_priv *pcm_priv,
		struct snd_soc_dai *dai)
{
	int ret;

	switch (pcm_priv->output_mode) {
	case O_MODE_I2S:
		/*ret = s900_i2s_4wire_config(pcm_priv, dai);*/
		/*snd_dai_writel(snd_dai_readl(PAD_CTL) | (0x1 << 1), PAD_CTL);*/
		if (dai_mode_i2s_count == 0) {
//			set_dai_reg_base(GPIO_MFP_NUM);
//			snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x1 << 2), MFP_CTL0);
//			snd_dai_writel(snd_dai_readl(MFP_CTL0) & ~(0x3 << 3), MFP_CTL0);

			/* disable i2s tx&rx */
			//printk("%s %d\n", __FUNCTION__, __LINE__);
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 0), I2S_CTL);

			/* reset i2s rx&&tx fifo, avoid left & right channel wrong */
			//printk("%s %d\n", __FUNCTION__, __LINE__);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
				& ~(0x3 << 9) & ~0x3, I2S_FIFOCTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL)
				| (0x3 << 9) | 0x3, I2S_FIFOCTL);

			/* this should before enable rx/tx,
			or after suspend, data may be corrupt */
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 11), I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 11), I2S_CTL);
			/* set i2s mode I2S_RX_ClkSel==1 */
			snd_dai_writel(snd_dai_readl(I2S_CTL) | (0x1 << 10), I2S_CTL);

			/* enable i2s rx/tx at the same time */
			snd_dai_writel(snd_dai_readl(I2S_CTL) | 0x3, I2S_CTL);

			/* i2s rx 00: 2.0-Channel Mode */
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x3 << 8), I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~(0x7 << 4), I2S_CTL);
			//printk("%s %d\n", __FUNCTION__, __LINE__);

			snd_dai_writel(0x0, I2STX_DAT);
			snd_dai_writel(0x0, I2STX_DAT);
			
		}
		dai_mode_i2s_count++;
		break;

	case O_MODE_HDMI:
		/* HDMI&SPDIF fifo reset */
		set_dai_reg_base(I2S_SPDIF_NUM);
		if (dai_mode_hdmi_count == 0) {
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x3,
				SPDIF_HDMI_CTL);
			/* HDMI fifo enable,DRQ enable */
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) |
				0x102, SPDIF_HDMI_CTL);
		}
		dai_mode_hdmi_count++;
		break;
	case O_MODE_SPDIF:
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int s900_dai_record_mode_unset(void)
{
	if (dai_mode_i2s_count > 0)
		dai_mode_i2s_count--;

	if (dai_mode_i2s_count == 0) {
		set_dai_reg_base(I2S_SPDIF_NUM);
		snd_dai_writel(snd_dai_readl(I2S_CTL) & ~0x3, I2S_CTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~0x3, I2S_FIFOCTL);
		snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9), I2S_FIFOCTL);
		/*pinctrl_put(pcm_priv->pc);*/
	}
	return 0;
}

static int s900_dai_mode_unset(struct s900_pcm_priv *pcm_priv)
{
	switch (pcm_priv->output_mode) {
	case O_MODE_I2S:
		if (dai_mode_i2s_count > 0)
			dai_mode_i2s_count--;

		if (dai_mode_i2s_count == 0) {
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(I2S_CTL) & ~0x3, I2S_CTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~0x3, I2S_FIFOCTL);
			snd_dai_writel(snd_dai_readl(I2S_FIFOCTL) & ~(0x3 << 9), I2S_FIFOCTL);
			/*pinctrl_put(pcm_priv->pc);*/
		}
		break;
	case O_MODE_HDMI:
		/* HDMI fifo disable */
		if (dai_mode_hdmi_count > 0)
			dai_mode_hdmi_count--;
		if (dai_mode_hdmi_count == 0) {
			set_dai_reg_base(I2S_SPDIF_NUM);
			snd_dai_writel(snd_dai_readl(SPDIF_HDMI_CTL) & ~0x2, SPDIF_HDMI_CTL);
		}
		break;
	case O_MODE_SPDIF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s900_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv
		= snd_soc_platform_get_drvdata(platform);

	//printk("%s %d\n", __FUNCTION__, __LINE__);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		break;
	default:
		return -EINVAL;
	}
	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream ) {
		//printk("%s %d\n", __FUNCTION__, __LINE__);
		s900_dai_record_clk_set(pcm_priv->output_mode, params_rate(params));
		//printk("%s %d\n", __FUNCTION__, __LINE__);
		s900_dai_record_mode_set(pcm_priv, dai);
	}

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream ) {
		//printk("%s %d\n", __FUNCTION__, __LINE__);
		s900_dai_clk_set(pcm_priv->output_mode, params_rate(params));
		//printk("%s %d\n", __FUNCTION__, __LINE__);
		s900_dai_mode_set(pcm_priv, dai);
	}
	//printk("%s %d\n", __FUNCTION__, __LINE__);
	return 0;
}

static int s900_dai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct s900_pcm_priv *pcm_priv =
		snd_soc_platform_get_drvdata(platform);

	//printk("%s %d\n", __FUNCTION__, __LINE__);
	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream ) {
		s900_dai_record_clk_disable();
		s900_dai_record_mode_unset();
	}

	//printk("%s %d\n", __FUNCTION__, __LINE__);
	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream ) {
		s900_dai_clk_disable(pcm_priv->output_mode);
		s900_dai_mode_unset(pcm_priv);
	}
	//printk("%s %d\n", __FUNCTION__, __LINE__);
	return 0;
}

struct snd_soc_dai_ops s900_dai_dai_ops = {
	.hw_params = s900_dai_hw_params,
	.hw_free = s900_dai_hw_free,
};

#define S900_STEREO_CAPTURE_RATES SNDRV_PCM_RATE_8000_96000
#define S900_STEREO_PLAYBACK_RATES SNDRV_PCM_RATE_8000_192000
#define S900_FORMATS SNDRV_PCM_FMTBIT_S32_LE

struct snd_soc_dai_driver s900_dai = {
	.name = "owl-audio-i2s",
	.id = S900_AIF_I2S,
	.playback = {
		.stream_name = "s900 dai Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = S900_STEREO_PLAYBACK_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "s900 dai Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = S900_STEREO_CAPTURE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &s900_dai_dai_ops,
};

static const struct snd_soc_component_driver s900_component = {
	.name		= "s900ac97c",
};

static const struct of_device_id owl_i2s_of_match[] = {
	{.compatible = "actions,owl-audio-i2s",},
	{}
};

MODULE_DEVICE_TABLE(of, owl_i2s_of_match);


struct dma_chan* dai_dma_slave_tx_chan()
{
	return dai_res.tx_chan;
}
EXEXPORT_SYMBOL_GPL(dai_dma_slave_tx_chan);

struct dma_chan* dai_dma_slave_rx_chan()
{
	return dai_res.rx_chan;
}
EXEXPORT_SYMBOL_GPL(dai_dma_slave_rx_chan);

struct dma_chan* dai_dma_slave_hdmia_chan()
{
	return dai_res.hdmia_chan;
}
EXEXPORT_SYMBOL_GPL(dai_dma_slave_hdmia_chan);


static int s900_dai_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct resource *res;
	int i;
	int ret = 0;

	struct device_node *dn;

	dn = of_find_compatible_node(NULL, NULL, "actions,owl-audio-i2s");
	if (!dn) {
		snd_err("Fail to get device_node actions,owl-audio-i2s\r\n");
		//goto of_get_failed;
	}
	
	/*FIXME: what if error in second or third loop*/
	//for(i=0; i<MAX_RES_NUM; i++) 
	for(i=0; i<1; i++) 
	{
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			snd_err("no memory resource i=%d\n", i);
			return -ENODEV;
		}

		if (!devm_request_mem_region (&pdev->dev, res->start,
					resource_size(res), "owl-audio-i2s")) {
			snd_err("Unable to request register region\n");
			return -EBUSY;
		}

		dai_res.base[i] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (dai_res.base[i] == NULL) {
			snd_err("Unable to ioremap register region\n");
			return -ENXIO;
		}
		
		printk(KERN_INFO"it's ok %d\n", i);
	}


	if (1)
	{
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++) 
		{
			ret = device_create_file(&pdev->dev, &dai_attr[i]);
			if (ret) {
				snd_err("Add device file failed");
				//goto device_create_file_failed;
			}
		}
	}
	else
	{
		snd_err("Find device failed");
		//goto err_bus_find_device;	
	}

	dai_res.i2stx_clk = devm_clk_get(&pdev->dev, "i2stx");
	if (IS_ERR(dai_res.i2stx_clk)) {
		snd_err("no i2stx clock defined\n");
		ret = PTR_ERR(dai_res.i2stx_clk);
		return ret;
	}

	
	dai_res.i2srx_clk = devm_clk_get(&pdev->dev, "i2srx");
	if (IS_ERR(dai_res.i2srx_clk)) {
		snd_err("no i2srx clock defined\n");
		ret = PTR_ERR(dai_res.i2srx_clk);
		
		devm_clk_put(&pdev->dev, dai_res.i2stx_clk);
		dai_res.i2stx_clk = NULL;
		dai_res.i2srx_clk = NULL;
		return ret;
	}
	
	/*FIXME: we STILL need spdif and hdmia clk!!!!!!!*/
	
	
	dai_res.tx_chan = dma_request_slave_channel(&pdev->dev, "tx");
	if(!dai_res.tx_chan)
	{
		dev_warn(&pdev->dev, "request tx chan failed\n");
		return ret;
	}
	
	dai_res.rx_chan = dma_request_slave_channel(&pdev->dev, "rx");
	if(!dai_res.rx_chan)
	{
		dev_warn(&pdev->dev, "request rx chan failed\n");
		return ret;	
	}
	
	dai_res.hdmia_chan = dma_request_slave_channel(&pdev->dev, "hdmia");
	if(!dai_res.hdmia_chan)
	{
		dev_warn(&pdev->dev, "request hdmia chan failed\n");
		return ret;	
	}
	
	
	dev_warn(&pdev->dev, "s900_dai_probe\n");
	//snd_err("dai probe fine\n");
	
	pdev->dev.init_name = "owl-audio-i2s";
	
	return snd_soc_register_component(&pdev->dev, &s900_component,
					 &s900_dai, 1);

}

static int s900_dai_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	//release virtual dma channels
	if(dai_res.tx_chan)
	{
		dma_release_channel(dai_res.tx_chan);
	}
	
	if(dai_res.rx_chan)
	{
		dma_release_channel(dai_res.rx_chan);
	}
	
	if(dai_res.hdmia_chan)
	{
		dma_release_channel(dai_res.hdmia_chan);
	}

	
	if(dai_res.i2stx_clk)
	{
		devm_clk_put(&pdev->dev, dai_res.i2stx_clk);
		dai_res.i2stx_clk = NULL;
	}

	if(dai_res.i2srx_clk)
	{
		devm_clk_put(&pdev->dev, dai_res.i2srx_clk);
		dai_res.i2srx_clk = NULL;
	}

	if(dai_res.spdif_clk)
	{
		devm_clk_put(&pdev->dev, dai_res.spdif_clk);
		dai_res.spdif_clk = NULL;
	}


	
	return 0;
}



static struct platform_driver s900_dai_driver = {
	.driver = {
		.name = "owl-audio-i2s",
		.owner = THIS_MODULE,
		.of_match_table = owl_i2s_of_match,
	},

	.probe = s900_dai_probe,
	.remove = s900_dai_remove,
};

static struct platform_device *s900_dai_device;

static int __init s900_dai_init(void)
{
	int ret;
	int i = 0;
	struct device *dev = NULL;

	ret = platform_driver_register(&s900_dai_driver);
	if (ret) {
		snd_err(
			"ASoC: Platform driver s900-dai register failed\n");
		goto err_driver_register;
	}


	return 0;

err_bus_find_device:
device_create_file_failed:
	platform_driver_unregister(&s900_dai_driver);
err_driver_register:
	return ret;
}

static void __exit s900_dai_exit(void)
{
	int i = 0;
	struct device *dev = NULL;

    dev = bus_find_device_by_name(&platform_bus_type, NULL, "owl-audio-i2s");	

	if (dev)
	{
		for (i = 0; i < ARRAY_SIZE(dai_attr); i++)
		{
			device_remove_file(dev, &dai_attr[i]);
		}
	}

	platform_driver_unregister(&s900_dai_driver);
	//platform_device_unregister(s900_dai_device);
	//s900_dai_device = NULL;
}

module_init(s900_dai_init);
module_exit(s900_dai_exit);


/* Module information */
MODULE_AUTHOR("sall.xie <sall.xie@actions-semi.com>");
MODULE_DESCRIPTION("S900 I2S Interface");
MODULE_LICENSE("GPL");
