// SPDX-License-Identifier: GPL-2.0+

#include <init.h>
#include <fdtdec.h>
#include <cpu_func.h>
#include <asm/arch-andes/csr.h>
#include <asm/csr.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

#define V821_R_PIO_BASE	0x42000540
#define V821_CCU_BASE	0x42001000
#define V821_AON_CCU_BASE	0x4a010000
#define V821_CSR_MXSTATUS	0x7c4
#define V821_MXSTATUS_ISAEE	BIT(22)

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

int dram_init_banksize(void)
{
	return fdtdec_setup_memory_banksize();
}

int board_init(void)
{
	return 0;
}

void harts_early_init(void)
{
	ulong val;

	if (!CONFIG_IS_ENABLED(RISCV_MMODE))
		return;

	/* Required before executing the vendor xandes DRAM code. */
	csr_set(V821_CSR_MXSTATUS, V821_MXSTATUS_ISAEE);

	/* Match the BSP: keep D-cache and coherence disabled in SPL. */
	val = csr_read(CSR_MCACHE_CTL);
	val |= MCACHE_CTL_IC_EN | MCACHE_CTL_CCTL_SUEN |
	       MCACHE_CTL_IC_PREFETCH_EN | MCACHE_CTL_DC_PREFETCH_EN |
	       MCACHE_CTL_DC_WAROUND_EN | MCACHE_CTL_L2C_WAROUND_EN;
	csr_write(CSR_MCACHE_CTL, val);

	csr_set(CSR_MMISC_CTL, MMISC_CTL_NON_BLOCKING_EN);
}

void board_debug_uart_init(void)
{
	u32 val;
	int i;

	/* The board uses a 40 MHz DCXO. */
	clrbits_le32((void __iomem *)(V821_AON_CCU_BASE + 0x404), BIT(31));

	/* Enable the A27 PLMT clock from HOSC. */
	val = readl((void __iomem *)(V821_CCU_BASE + 0x10));
	if (!(val & BIT(31)))
		writel(BIT(31), (void __iomem *)(V821_CCU_BASE + 0x10));

	/* Enable PLL_PERI / 16 as the 192 MHz UART module clock. */
	val = readl((void __iomem *)(V821_AON_CCU_BASE + 0x20));
	if (!(val & BIT(31))) {
		val &= ~(GENMASK(15, 8) | GENMASK(2, 0));
		val &= ~BIT(27);
		val |= 191U << 8 | 4U | BIT(31) | BIT(30) | BIT(29);
		writel(val, (void __iomem *)(V821_AON_CCU_BASE + 0x20));

		for (i = 0; i < 0x10000; i++) {
			if (readl((void __iomem *)(V821_AON_CCU_BASE + 0x20)) & BIT(28))
				break;
		}

		if (!(readl((void __iomem *)(V821_AON_CCU_BASE + 0x20)) & BIT(28)))
			return;

		setbits_le32((void __iomem *)(V821_AON_CCU_BASE + 0x20), BIT(27));
	}
	writel(0xffff, (void __iomem *)(V821_AON_CCU_BASE + 0x24));

	/* Select PLL_PERI 192 MHz on the APB special clock. */
	val = readl((void __iomem *)(V821_AON_CCU_BASE + 0x580));
	val &= ~(GENMASK(4, 0) | GENMASK(26, 24));
	val |= 0x3U << 24;
	writel(val, (void __iomem *)(V821_AON_CCU_BASE + 0x580));

	/* PL4: UART0 TX, PL5: UART0 RX, function 3. */
	val = readl((void __iomem *)V821_R_PIO_BASE);
	val &= ~(0xfU << 16 | 0xfU << 20);
	val |= 3U << 16 | 3U << 20;
	writel(val, (void __iomem *)V821_R_PIO_BASE);

	/* Pull up PL4/PL5. */
	val = readl((void __iomem *)(V821_R_PIO_BASE + 0x24));
	val &= ~(3U << 8 | 3U << 10);
	val |= 1U << 8 | 1U << 10;
	writel(val, (void __iomem *)(V821_R_PIO_BASE + 0x24));

	/* Release UART0 reset and enable its bus clock. */
	setbits_le32((void __iomem *)(V821_CCU_BASE + 0x90), BIT(15));
	clrbits_le32((void __iomem *)(V821_CCU_BASE + 0x80), BIT(15));
	for (i = 0; i < 100; i++)
		;
	setbits_le32((void __iomem *)(V821_CCU_BASE + 0x80), BIT(15));
}
