/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _ASM_ARCH_SUNXI_CLOCK_H
#define _ASM_ARCH_SUNXI_CLOCK_H

#include <asm/io.h>
#include <asm/arch-sunxi/cpu.h>
#include <linux/bitops.h>

#define SUNXI_CCU_PLL_PERI_CTRL0	0x0010
#define SUNXI_CCU_MMC0_CLK		0x0014
#define SUNXI_CCU_SPI0_CLK		0x001c
#define SUNXI_CCU_SPIF_CLK		0x0020
#define SUNXI_CCU_BUS_GATE0		0x0080
#define SUNXI_CCU_BUS_GATE1		0x0084
#define SUNXI_CCU_BUS_RST0		0x0090
#define SUNXI_CCU_BUS_RST1		0x0094

#define CCM_MMC_CTRL_OSCM24		(0 << 24)
#define CCM_MMC_CTRL_PLL6		BIT(24)
#define CCM_MMC_CTRL_ENABLE		BIT(31)
#define CCM_MMC_CTRL_N(x)		((x) << 16)
#define CCM_MMC_CTRL_M(x)		((x) - 1)
#define CCM_MMC_CTRL_OCLK_DLY(x)	((void)(x), 0)
#define CCM_MMC_CTRL_SCLK_DLY(x)	((void)(x), 0)

static inline unsigned int clock_get_pll6(void)
{
	u32 val = readl((void __iomem *)SUNXI_CCU_APP_BASE +
			SUNXI_CCU_PLL_PERI_CTRL0);
	unsigned int n = (val >> 8) & 0xff;
	unsigned int m = val & 0x7;

	return 48000000 * 2 * (n + 1) / (m + 1);
}

#endif
