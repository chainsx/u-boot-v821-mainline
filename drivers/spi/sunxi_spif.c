// SPDX-License-Identifier: GPL-2.0+

#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <reset.h>
#include <spi.h>
#include <spi-mem.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/sizes.h>

#define SPIF_VER_REG			0x00
#define SPIF_GC_REG			0x04
#define SPIF_GCA_REG			0x08
#define SPIF_TC_REG			0x0c
#define SPIF_INT_EN_REG			0x14
#define SPIF_INT_STA_REG		0x18
#define SPIF_CSD_REG			0x1c
#define SPIF_PHC_REG			0x20
#define SPIF_TCF_REG			0x24
#define SPIF_TCS_REG			0x28
#define SPIF_TNM_REG			0x2c
#define SPIF_DMA_CTL_REG		0x40
#define SPIF_DSC_REG			0x44

#define SPIF_GC_DMA_MODE		BIT(0)
#define SPIF_GC_NMODE_EN		BIT(2)
#define SPIF_GC_PMODE_EN		BIT(3)
#define SPIF_GC_CPHA			BIT(4)
#define SPIF_GC_CPOL			BIT(5)
#define SPIF_GC_SS_MASK			GENMASK(7, 6)
#define SPIF_GC_CS_POL			BIT(8)
#define SPIF_GC_HOLD_EN			BIT(13)
#define SPIF_GC_WP_EN			BIT(15)
#define SPIF_GC_DTR_EN			BIT(16)
#define SPIF_GC_RX_CFG_FBS		BIT(17)
#define SPIF_GC_TX_CFG_FBS		BIT(18)

#define SPIF_FIFO_SRST			GENMASK(1, 0)
#define SPIF_DIGITAL_ANALOG_EN		BIT(20)
#define SPIF_DIGITAL_DELAY_MASK		GENMASK(18, 16)
#define SPIF_ANALOG_DL_SW_RX_EN		BIT(6)
#define SPIF_ANALOG_DELAY_MASK		GENMASK(5, 0)
#define SPIF_ANALOG_DELAY_DEFAULT	16
#define SPIF_CSD_DEFAULT		0x00050606
#define SPIF_DMA_DONE			BIT(24)
#define SPIF_DMA_DESC_LEN		(32 << 4)

#define SPIF_CMD_TRANS_EN		BIT(28)
#define SPIF_ADDR_TRANS_EN		BIT(24)
#define SPIF_DUMMY_TRANS_EN		BIT(16)
#define SPIF_RX_TRANS_EN		BIT(8)
#define SPIF_TX_TRANS_EN		BIT(12)

#define SPIF_CMD_WIDTH_POS		6
#define SPIF_ADDR_WIDTH_POS		4
#define SPIF_DATA_WIDTH_POS		0
#define SPIF_CMD_OPCODE_POS		24
#define SPIF_DUMMY_NUM_POS		16
#define SPIF_ADDR_32BIT			BIT(24)
#define SPIF_ADDR_SIZE_24BIT_V2		(2 << 24)
#define SPIF_ADDR_SIZE_32BIT_V2		(3 << 24)
#define SPIF_VERSION_V2			0x10002
#define SPIF_DESC_NORMAL		BIT(28)

#define SPIF_DMA_READ_FLASH		BIT(1)
#define SPIF_DMA_LAST_DESC		BIT(0)
#define SPIF_DMA_BURST_INCR16		(7 << 4)
#define SPIF_DMA_BLOCK_64		(3 << 24)

#define SPIF_MAX_CHUNK			SZ_4K

struct sunxi_spif_desc {
	u32 flags;
	u32 block_data_len;
	u32 data_addr;
	u32 next_desc;
	u32 phases;
	u32 flash_addr;
	u32 widths;
	u32 counts;
};

struct sunxi_spif_priv {
	void __iomem *base;
	struct clk ahb_clk;
	struct clk mod_clk;
	struct reset_ctl reset;
	uint frequency;
	uint mode;
	bool addr_size_v2;
};

static u8 spif_bounce[SPIF_MAX_CHUNK]
	__aligned(ARCH_DMA_MINALIGN);
static struct sunxi_spif_desc spif_desc
	__aligned(ARCH_DMA_MINALIGN);

static int sunxi_spif_wait_clear(void __iomem *base, u32 offset, u32 mask)
{
	int timeout = 1000000;

	while (readl(base + offset) & mask) {
		if (!timeout--)
			return -ETIMEDOUT;
		udelay(1);
	}

	return 0;
}

static int sunxi_spif_wait_set(void __iomem *base, u32 offset, u32 mask)
{
	int timeout = 1000000;

	while (!(readl(base + offset) & mask)) {
		if (!timeout--)
			return -ETIMEDOUT;
		udelay(1);
	}

	return 0;
}

static int sunxi_spif_hw_init(void __iomem *base)
{
	int ret;
	u32 val;

	writel(0, base + SPIF_INT_EN_REG);
	setbits_le32(base + SPIF_GCA_REG, SPIF_FIFO_SRST);
	ret = sunxi_spif_wait_clear(base, SPIF_GCA_REG, SPIF_FIFO_SRST);
	if (ret)
		return ret;

	val = readl(base + SPIF_GC_REG);
	val &= ~(SPIF_GC_DMA_MODE | SPIF_GC_NMODE_EN | SPIF_GC_PMODE_EN |
		 SPIF_GC_CPHA | SPIF_GC_CPOL | SPIF_GC_SS_MASK |
		 SPIF_GC_HOLD_EN | SPIF_GC_WP_EN | SPIF_GC_DTR_EN |
		 SPIF_GC_RX_CFG_FBS | SPIF_GC_TX_CFG_FBS);
	val |= SPIF_GC_CS_POL;
	writel(val, base + SPIF_GC_REG);

	writel(SPIF_CSD_DEFAULT, base + SPIF_CSD_REG);
	clrsetbits_le32(base + SPIF_TC_REG, SPIF_DIGITAL_DELAY_MASK,
			SPIF_DIGITAL_ANALOG_EN);
	clrsetbits_le32(base + SPIF_TC_REG, SPIF_ANALOG_DELAY_MASK,
			SPIF_ANALOG_DL_SW_RX_EN | SPIF_ANALOG_DELAY_DEFAULT);
	mdelay(1);

	return 0;
}

static u32 sunxi_spif_addr_size(const struct sunxi_spif_priv *priv,
				unsigned int nbytes)
{
	if (!priv->addr_size_v2)
		return nbytes == 4 ? SPIF_ADDR_32BIT : 0;

	return nbytes == 4 ? SPIF_ADDR_SIZE_32BIT_V2 :
			     SPIF_ADDR_SIZE_24BIT_V2;
}

static int sunxi_spif_reset_fifo(void __iomem *base)
{
	setbits_le32(base + SPIF_GCA_REG, SPIF_FIFO_SRST);

	return sunxi_spif_wait_clear(base, SPIF_GCA_REG, SPIF_FIFO_SRST);
}

static int sunxi_spif_set_speed(struct udevice *dev, uint hz)
{
	struct sunxi_spif_priv *priv = dev_get_priv(dev);
	ulong rate;

	if (!hz)
		return -EINVAL;

	rate = clk_set_rate(&priv->mod_clk, hz);
	if (IS_ERR_VALUE(rate))
		return rate;

	priv->frequency = rate;

	return 0;
}

static int sunxi_spif_set_mode(struct udevice *dev, uint mode)
{
	struct sunxi_spif_priv *priv = dev_get_priv(dev);

	if (mode & ~(SPI_CPOL | SPI_CPHA))
		return -EINVAL;

	priv->mode = mode;

	return 0;
}

static int sunxi_spif_width_value(unsigned int width)
{
	switch (width) {
	case 1:
	case 2:
	case 4:
	case 8:
		return ilog2(width);
	default:
		return -EINVAL;
	}
}

static bool sunxi_spif_supports_op(struct spi_slave *slave,
				   const struct spi_mem_op *op)
{
	if (op->cmd.nbytes != 1 || op->cmd.opcode > U8_MAX)
		return false;

	if ((op->addr.nbytes && sunxi_spif_width_value(op->addr.buswidth) < 0) ||
	    (op->dummy.nbytes && sunxi_spif_width_value(op->dummy.buswidth) < 0) ||
	    (op->data.nbytes && sunxi_spif_width_value(op->data.buswidth) < 0))
		return false;

	if (op->addr.nbytes > 4 || op->dummy.nbytes > 31 ||
	    (op->addr.nbytes && op->addr.nbytes < 3))
		return false;

	if (op->cmd.dtr || op->addr.dtr || op->dummy.dtr || op->data.dtr)
		return false;

	return true;
}

static int sunxi_spif_adjust_op_size(struct spi_slave *slave,
				     struct spi_mem_op *op)
{
	op->data.nbytes = min(op->data.nbytes, (unsigned int)SPIF_MAX_CHUNK);

	return 0;
}

static void sunxi_spif_flush_range(ulong start, size_t size)
{
	ulong end = ALIGN(start + size, ARCH_DMA_MINALIGN);

	flush_dcache_range(ALIGN_DOWN(start, ARCH_DMA_MINALIGN), end);
}

static void sunxi_spif_invalidate_range(ulong start, size_t size)
{
	ulong end = ALIGN(start + size, ARCH_DMA_MINALIGN);

	invalidate_dcache_range(ALIGN_DOWN(start, ARCH_DMA_MINALIGN), end);
}

static int sunxi_spif_exec_op(struct spi_slave *slave,
			      const struct spi_mem_op *op)
{
	struct udevice *dev = slave->dev->parent;
	struct sunxi_spif_priv *priv = dev_get_priv(dev);
	void __iomem *base = priv->base;
	int cmd_width, addr_width, dummy_width, data_width;
	u32 phases = 0, widths = 0, counts = 0;
	int ret;

	ret = sunxi_spif_reset_fifo(base);
	if (ret)
		return ret;

	cmd_width = sunxi_spif_width_value(op->cmd.buswidth);
	addr_width = op->addr.nbytes ?
		     sunxi_spif_width_value(op->addr.buswidth) : 0;
	dummy_width = op->dummy.nbytes ?
		      sunxi_spif_width_value(op->dummy.buswidth) : 0;
	data_width = op->data.nbytes ?
		     sunxi_spif_width_value(op->data.buswidth) : 0;
	if (cmd_width < 0 || addr_width < 0 || dummy_width < 0 ||
	    data_width < 0)
		return -EINVAL;

	memset(&spif_desc, 0, sizeof(spif_desc));

	widths = op->cmd.opcode << SPIF_CMD_OPCODE_POS |
		 cmd_width << SPIF_CMD_WIDTH_POS;
	if (op->cmd.buswidth != 1)
		widths |= cmd_width << SPIF_DATA_WIDTH_POS;
	phases |= SPIF_CMD_TRANS_EN;

	if (op->addr.nbytes) {
		phases |= SPIF_ADDR_TRANS_EN;
		widths |= addr_width << SPIF_ADDR_WIDTH_POS;
		counts |= sunxi_spif_addr_size(priv, op->addr.nbytes);
	}

	if (op->dummy.nbytes) {
		phases |= SPIF_DUMMY_TRANS_EN;
		counts |= (op->dummy.nbytes * 8 / op->dummy.buswidth) <<
			  SPIF_DUMMY_NUM_POS;
	}

	spif_desc.flags = SPIF_DMA_BURST_INCR16 | SPIF_DMA_LAST_DESC;
	spif_desc.block_data_len = SPIF_DMA_BLOCK_64;
	spif_desc.phases = phases;
	spif_desc.flash_addr = op->addr.val;
	spif_desc.widths = widths;

	if (op->data.nbytes) {
		if (op->data.nbytes > SPIF_MAX_CHUNK)
			return -EINVAL;

		widths |= data_width << SPIF_DATA_WIDTH_POS;
		spif_desc.widths = widths;
		counts |= op->data.nbytes;
		spif_desc.block_data_len |= op->data.nbytes;
		spif_desc.data_addr = (uintptr_t)spif_bounce >> 2;

		if (op->data.dir == SPI_MEM_DATA_IN) {
			phases |= SPIF_RX_TRANS_EN;
			spif_desc.phases = phases;
			spif_desc.flags |= SPIF_DMA_READ_FLASH;
			sunxi_spif_invalidate_range((ulong)spif_bounce,
						    op->data.nbytes);
		} else {
			phases |= SPIF_TX_TRANS_EN;
			spif_desc.phases = phases;
			memcpy(spif_bounce, op->data.buf.out,
			       op->data.nbytes);
			sunxi_spif_flush_range((ulong)spif_bounce,
					       op->data.nbytes);
		}
	}

	spif_desc.counts = SPIF_DESC_NORMAL | counts;

	clrsetbits_le32(base + SPIF_GC_REG, SPIF_GC_CPHA | SPIF_GC_CPOL,
			(priv->mode & SPI_CPOL ? SPIF_GC_CPOL : 0) |
			(priv->mode & SPI_CPHA ? SPIF_GC_CPHA : 0));

	if (!op->data.nbytes) {
		clrbits_le32(base + SPIF_GC_REG, SPIF_GC_DMA_MODE);
		writel(spif_desc.phases, base + SPIF_PHC_REG);
		writel(spif_desc.flash_addr, base + SPIF_TCF_REG);
		writel(spif_desc.widths, base + SPIF_TCS_REG);
		writel(spif_desc.counts, base + SPIF_TNM_REG);
		setbits_le32(base + SPIF_GC_REG, SPIF_GC_NMODE_EN);

		ret = sunxi_spif_wait_clear(base, SPIF_GC_REG,
					    SPIF_GC_NMODE_EN);
		if (ret) {
			int hw_ret = sunxi_spif_hw_init(base);

			return hw_ret ? hw_ret : ret;
		}

		return 0;
	}

	sunxi_spif_flush_range((ulong)&spif_desc, sizeof(spif_desc));

	setbits_le32(base + SPIF_GC_REG, SPIF_GC_DMA_MODE);
	writel(spif_desc.phases, base + SPIF_PHC_REG);
	writel(spif_desc.flash_addr, base + SPIF_TCF_REG);
	writel(spif_desc.widths, base + SPIF_TCS_REG);
	writel(spif_desc.counts, base + SPIF_TNM_REG);
	writel((uintptr_t)&spif_desc >> 2, base + SPIF_DSC_REG);
	writel(SPIF_DMA_DESC_LEN, base + SPIF_DMA_CTL_REG);
	setbits_le32(base + SPIF_DMA_CTL_REG, BIT(0));

	ret = sunxi_spif_wait_set(base, SPIF_INT_STA_REG, SPIF_DMA_DONE);
	writel(SPIF_DMA_DONE, base + SPIF_INT_STA_REG);
	if (ret) {
		int hw_ret = sunxi_spif_hw_init(base);

		return hw_ret ? hw_ret : ret;
	}

	if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes) {
		sunxi_spif_invalidate_range((ulong)spif_bounce,
					    op->data.nbytes);
		memcpy(op->data.buf.in, spif_bounce, op->data.nbytes);
	}

	return 0;
}

static const struct spi_controller_mem_ops sunxi_spif_mem_ops = {
	.adjust_op_size = sunxi_spif_adjust_op_size,
	.supports_op = sunxi_spif_supports_op,
	.exec_op = sunxi_spif_exec_op,
};

static const struct dm_spi_ops sunxi_spif_ops = {
	.set_speed = sunxi_spif_set_speed,
	.set_mode = sunxi_spif_set_mode,
	.mem_ops = &sunxi_spif_mem_ops,
};

static int sunxi_spif_probe(struct udevice *dev)
{
	struct sunxi_spif_priv *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -ENOENT;

	ret = clk_get_by_name(dev, "ahb", &priv->ahb_clk);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "mod", &priv->mod_clk);
	if (ret)
		return ret;

	ret = reset_get_by_index(dev, 0, &priv->reset);
	if (ret)
		return ret;

	ret = clk_enable(&priv->ahb_clk);
	if (ret)
		return ret;

	ret = clk_enable(&priv->mod_clk);
	if (ret)
		goto err_ahb_clk;

	ret = reset_deassert(&priv->reset);
	if (ret)
		goto err_mod_clk;

	ret = sunxi_spif_hw_init(priv->base);
	if (ret)
		goto err_mod_clk;

	priv->addr_size_v2 = readl(priv->base + SPIF_VER_REG) >=
			     SPIF_VERSION_V2;

	return 0;

err_mod_clk:
	clk_disable(&priv->mod_clk);
err_ahb_clk:
	clk_disable(&priv->ahb_clk);
	return ret;
}

static const struct udevice_id sunxi_spif_ids[] = {
	{ .compatible = "allwinner,sun300i-v821-spif" },
	{ }
};

U_BOOT_DRIVER(sunxi_spif) = {
	.name = "sunxi_spif",
	.id = UCLASS_SPI,
	.of_match = sunxi_spif_ids,
	.ops = &sunxi_spif_ops,
	.priv_auto = sizeof(struct sunxi_spif_priv),
	.probe = sunxi_spif_probe,
};
