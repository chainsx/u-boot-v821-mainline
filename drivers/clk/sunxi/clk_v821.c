// SPDX-License-Identifier: GPL-2.0+

#include <clk-uclass.h>
#include <dm.h>
#include <clk/sunxi.h>
#include <asm/io.h>
#include <dt-bindings/clock/sun300i-v821-ccu.h>
#include <dt-bindings/reset/sun300i-v821-ccu.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

#define V821_SPIF_CLK_SRC_MASK		GENMASK(25, 24)
#define V821_SPIF_CLK_DIV_N_MASK	GENMASK(17, 16)
#define V821_SPIF_CLK_DIV_M_MASK	GENMASK(3, 0)

static struct ccu_clk_gate v821_gates[] = {
	[CLK_MMC0]		= GATE(0x14, BIT(31)),
	[CLK_MMC1]		= GATE(0x5c, BIT(31)),
	[CLK_SPI0]		= GATE(0x1c, BIT(31)),
	[CLK_SPIF]		= GATE(0x20, BIT(31)),
	[CLK_BUS_MMC0]		= GATE(0x84, BIT(20)),
	[CLK_BUS_MMC1]		= GATE(0x84, BIT(21)),
	[CLK_BUS_UART0]		= GATE(0x80, BIT(15)),
	[CLK_BUS_UART1]		= GATE(0x80, BIT(16)),
	[CLK_BUS_UART2]		= GATE(0x80, BIT(17)),
	[CLK_BUS_UART3]		= GATE(0x80, BIT(18)),
	[CLK_BUS_SPI0]		= GATE(0x84, BIT(4)),
	[CLK_BUS_SPIF]		= GATE(0x84, BIT(5)),
};

static struct ccu_reset v821_resets[] = {
	[RST_BUS_MMC0]		= RESET(0x94, BIT(20)),
	[RST_BUS_MMC1]		= RESET(0x94, BIT(21)),
	[RST_BUS_UART0]		= RESET(0x90, BIT(15)),
	[RST_BUS_UART1]		= RESET(0x90, BIT(16)),
	[RST_BUS_UART2]		= RESET(0x90, BIT(17)),
	[RST_BUS_UART3]		= RESET(0x90, BIT(18)),
	[RST_BUS_SPI0]		= RESET(0x94, BIT(4)),
	[RST_BUS_SPIF]		= RESET(0x94, BIT(5)),
};

static ulong v821_spif_best_rate(ulong rate, u32 *config)
{
	u32 best = 0;
	ulong best_rate = 0;

	unsigned int n;

	for (n = 0; n < 4; n++) {
		ulong parent = 384000000 >> n;
		u32 m = DIV_ROUND_UP(parent, rate);
		ulong actual;

		if (!m || m > 16)
			continue;

		actual = parent / m;
		if (actual > rate)
			continue;

		if (!best || rate - actual < rate - best_rate) {
			best = 2 << 24 | n << 16 | (m - 1);
			best_rate = actual;
		}
	}

	if (best)
		*config = best;

	return best_rate;
}

ulong sunxi_clk_set_rate(struct clk *clk, ulong rate)
{
	struct ccu_plat *plat = dev_get_plat(clk->dev);
	const struct ccu_clk_gate *gate;
	u32 mask, config = 0;
	ulong actual;
	void __iomem *reg;

	if (!rate)
		return -EINVAL;

	if (clk->id != CLK_SPIF)
		return -ENOSYS;

	gate = &plat->desc->gates[clk->id];
	reg = plat->base + gate->off;

	mask = V821_SPIF_CLK_SRC_MASK | V821_SPIF_CLK_DIV_N_MASK |
	       V821_SPIF_CLK_DIV_M_MASK;
	actual = v821_spif_best_rate(rate, &config);

	if (!actual)
		return -EINVAL;

	clrsetbits_le32(reg, mask, config);

	return actual;
}

const struct ccu_desc v821_ccu_desc = {
	.gates = v821_gates,
	.resets = v821_resets,
	.num_gates = ARRAY_SIZE(v821_gates),
	.num_resets = ARRAY_SIZE(v821_resets),
};
