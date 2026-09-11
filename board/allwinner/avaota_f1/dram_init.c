// SPDX-License-Identifier: GPL-2.0+

#include <hang.h>
#include <init.h>
#include <linux/delay.h>
#include <sunxi_image.h>
#include <asm/global_data.h>
#include <asm/io.h>

#include "dram.h"

#define V821_SPL_ADDR	0x02000000

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
	},
};

int spl_board_init_f(void)
{
	struct boot_file_head *spl = (void *)V821_SPL_ADDR;
	int ret;

	ret = init_DRAM(0, &avaota_f1_dram);
	if (ret <= 0)
		hang();

	writel(ret, &spl->dram_size);
	gd->ram_size = (phys_size_t)ret << 20;

	return 0;
}
