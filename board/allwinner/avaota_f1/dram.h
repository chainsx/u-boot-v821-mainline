/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __AVAOTA_F1_DRAM_H
#define __AVAOTA_F1_DRAM_H

struct v821_dram_para {
	u32 clk;
	u32 type;
	u32 zq;
	u32 odt_en;
	u32 para1;
	u32 para2;
	u32 mr0;
	u32 mr1;
	u32 mr2;
	u32 mr3;
	u32 tpr[14];
};

int init_DRAM(int type, struct v821_dram_para *para);

#endif
