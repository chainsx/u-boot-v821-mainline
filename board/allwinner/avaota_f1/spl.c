// SPDX-License-Identifier: GPL-2.0+

#include <spl.h>
#include <sunxi_image.h>
#include <asm/io.h>
#include <asm/arch-sunxi/bootrom.h>
#include <linux/kernel.h>

#define V821_SPL_MMC_SECTOR	16

u32 spl_boot_device(void)
{
	struct boot_file_head *header = (void *)V821_SPL_ADDR;

	if (readb(&header->boot_media) == V821_BOOT_MEDIA_SPI)
		return BOOT_DEVICE_SPI;

	return BOOT_DEVICE_MMC1;
}

unsigned long board_spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
						 unsigned long raw_sect)
{
	struct boot_file_head *header = (void *)V821_SPL_ADDR;
	ulong spl_size = readl(&header->length);
	ulong sector = max(raw_sect, spl_size / 512);

	sector = max(sector, (ulong)(CONFIG_SPL_PAD_TO / 512));

	return V821_SPL_MMC_SECTOR + max(sector, 0x20UL);
}
