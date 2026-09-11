// SPDX-License-Identifier: GPL-2.0+

#include <asm/io.h>
#include <linux/bitops.h>

#define V821_PRCM_BASE		0x4a000000
#define V821_PRCM_RESET_CTRL	(V821_PRCM_BASE + 0x1c)
#define V821_WDT_BASE		0x4a001000
#define V821_PRCM_WDT_RESET_EN	BIT(3)
#define V821_WDT_SOFT_RESET	0x16aa0001

void reset_cpu(void)
{
	writel(readl((void __iomem *)V821_PRCM_BASE) |
	       V821_PRCM_WDT_RESET_EN,
	       (void __iomem *)V821_PRCM_RESET_CTRL);
	writel(V821_WDT_SOFT_RESET,
	       (void __iomem *)(V821_WDT_BASE + 0x08));

	while (1)
		;
}
