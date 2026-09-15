// SPDX-License-Identifier: GPL-2.0+

#include <hang.h>
#include <init.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <sunxi_image.h>
#include <asm/global_data.h>
#include <asm/io.h>

#include "dram.h"

#define V821_SPL_ADDR	0x02000000
#define V821_AON_CCU_BASE	0x4a010000

#define V821_PLL_CPU_CTRL	(V821_AON_CCU_BASE + 0x000)
#define V821_PLL_PERI_CTRL	(V821_AON_CCU_BASE + 0x020)
#define V821_AHB_CLK_REG	(V821_AON_CCU_BASE + 0x500)
#define V821_APB_CLK_REG	(V821_AON_CCU_BASE + 0x504)
#define V821_A27L2_CLK_REG	(V821_AON_CCU_BASE + 0x588)

#define V821_PLL_CPU_LDO_EN	BIT(30)
#define V821_PLL_CPU_EN		BIT(31)
#define V821_PLL_CPU_LOCK_EN	BIT(29)
#define V821_PLL_CPU_LOCK	BIT(28)
#define V821_PLL_CPU_OUTPUT_GATE	BIT(27)
#define V821_PLL_CPU_LOCK_TIME	GENMASK(26, 24)
#define V821_PLL_CPU_N		GENMASK(15, 8)
#define V821_PLL_CPU_INPUT_DIV	GENMASK(3, 2)

DECLARE_GLOBAL_DATA_PTR;

void __usdelay(unsigned long us)
{
	udelay(us);
}

static struct v821_dram_para avaota_f1_dram = {
	.clk = 528,
	.type = 2,
	.zq = 0x7b7bf9,
	.odt_en = 0,
	.para1 = 0xd2,
	.para2 = 0x400000,
	.mr0 = 0xe73,
	.mr1 = 0x2,
	.mr2 = 0,
	.mr3 = 0,
	.tpr = {
		0x471992, 0x131a10c, 0x57041, 0xb4787896, 0,
		0x48484848, 0x48, 0x1621121e,
		0, 0, 0, 0, 0, 0x34000100,
	},
};

/*
 * The boot ROM starts the A27 cluster from HOSC.  BSP boot0 raises PLL_CPU
 * to 960 MHz before loading the next stage; do the same before entering
 * OpenSBI so that firmware, U-Boot and Linux do not run at 40 MHz.
 */
static void spl_raise_cpu_clock(void)
{
	void __iomem *pll = (void __iomem *)V821_PLL_CPU_CTRL;
	u32 val;
	int i, locked;

	val = readl(pll);
	val |= V821_PLL_CPU_LDO_EN;
	val &= ~(V821_PLL_CPU_EN | V821_PLL_CPU_LOCK_EN |
		 V821_PLL_CPU_OUTPUT_GATE);
	writel(val, pll);

	for (i = 0; i < 100; i++) {
		if (!(readl(pll) & (V821_PLL_CPU_EN | V821_PLL_CPU_LOCK_EN |
				    V821_PLL_CPU_OUTPUT_GATE)))
			break;
		udelay(3);
	}

	val = readl(pll);
	val &= ~(V821_PLL_CPU_LOCK_TIME | V821_PLL_CPU_INPUT_DIV |
		 V821_PLL_CPU_N);
	val |= FIELD_PREP(V821_PLL_CPU_LOCK_TIME, 3) |
	       FIELD_PREP(V821_PLL_CPU_INPUT_DIV, 1) |
	       FIELD_PREP(V821_PLL_CPU_N, 47);
	writel(val, pll);

	val |= V821_PLL_CPU_EN | V821_PLL_CPU_LOCK_EN;
	writel(val, pll);

	locked = 0;
	for (i = 0; i < 100 && locked < 3; i++) {
		if (readl(pll) & V821_PLL_CPU_LOCK)
			locked++;
		else
			locked = 0;
		udelay(3);
	}
	if (locked < 3)
		return;

	udelay(20);
	setbits_le32(pll, V821_PLL_CPU_OUTPUT_GATE);

	/* Keep L2 on the CPU PLL, matching the BSP clock topology. */
	val = readl((void __iomem *)V821_A27L2_CLK_REG);
	val &= ~GENMASK(31, 24);
	val |= BIT(31) | 4U << 24;
	writel(val, (void __iomem *)V821_A27L2_CLK_REG);

	/* AHB = PLL_PERI 768 MHz / 4, APB = PLL_PERI 384 MHz / 4. */
	if (readl((void __iomem *)V821_PLL_PERI_CTRL) & V821_PLL_CPU_LOCK) {
		val = readl((void __iomem *)V821_AHB_CLK_REG);
		val &= ~(GENMASK(25, 24) | GENMASK(4, 0));
		val |= 1U << 24 | 3U;
		writel(val, (void __iomem *)V821_AHB_CLK_REG);

		val = readl((void __iomem *)V821_APB_CLK_REG);
		val &= ~(GENMASK(25, 24) | GENMASK(4, 0));
		val |= 1U << 24 | 3U;
		writel(val, (void __iomem *)V821_APB_CLK_REG);
	}
}

int spl_board_init_f(void)
{
	struct boot_file_head *spl = (void *)V821_SPL_ADDR;
	int ret;

	ret = init_DRAM(0, &avaota_f1_dram);
	if (ret <= 0)
		hang();

	writel(ret, &spl->dram_size);
	gd->ram_size = (phys_size_t)ret << 20;

	spl_raise_cpu_clock();

	return 0;
}
