// SPDX-License-Identifier: GPL-2.0+

#include <env.h>
#include <asm/arch-sunxi/bootrom.h>
#include <asm/io.h>
#include <sunxi_image.h>

int board_late_init(void)
{
	const struct boot_file_head *header = (void *)V821_SPL_ADDR;

	if (readb(&header->boot_media) == V821_BOOT_MEDIA_SPI)
		env_set("boot_source", "spi");
	else
		env_set("boot_source", "mmc");

	return 0;
}
