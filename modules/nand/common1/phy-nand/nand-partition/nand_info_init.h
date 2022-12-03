/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NAND_INFO_INIT_H__
#define __NAND_INFO_INIT_H__

#include "phy.h"

int nand_info_init_v1(struct _nand_info *nand_info, uchar chip, uint16 start_block, uchar *mbr_data);
int test_mbr(uchar *data);
int write_mbr(struct _nand_info *nand_info);
int write_factory_block_table(struct _nand_info *nand_info);
int print_factory_block_table(struct _nand_info *nand_info);
int write_new_block_table(struct _nand_info *nand_info);
int write_no_use_block(struct _nand_info *nand_info);
unsigned short read_new_bad_block_table(struct _nand_info *nand_info);
void print_nand_info(struct _nand_info *nand_info);
unsigned int calc_crc32(void *buffer, unsigned int length);
int build_all_phy_partition(struct _nand_info *nand_info);
struct _nand_phy_partition *get_head_phy_partition_from_nand_info(struct _nand_info *nand_info);
void set_cache_level(struct _nand_info *nand_info, unsigned short cache_level);
void set_capacity_level(struct _nand_info *nand_info, unsigned short capacity_level);
void set_read_reclaim_interval(struct _nand_info *nand_info, unsigned int read_reclaim_interval);
unsigned short debug_read_block(struct _nand_info *nand_info, unsigned int nDieNum, unsigned int nBlkNum);
void debug_read_chip(struct _nand_info *nand_info);
#endif /*NAND_INFO_INIT_H*/
