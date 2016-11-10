/*
 * arch/powerpc/math-emu/efdmul.c
 *
 * Copyright (C) 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Ebony Zhu, ebony.zhu@freescale.com
 *
 * Description:
 * This file is the implementation of SPE instruction "efdmul"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */    

#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "spe.h"
#include "soft-fp.h"
#include "double.h"

int
efdmul(void *rD, void *rA, void *rB)
{
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	int ret = 0;

#ifdef DEBUG
	printk("%s: %p %p %p\n", __FUNCTION__, rD, rA, rB);
#endif

	__FP_UNPACK_D(A, rA);
	__FP_UNPACK_D(B, rB);

#ifdef DEBUG
	printk("A: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       A_s, A_f1, A_f0, A_e, A_c, A_f1, A_f0, A_e + 1023);
	printk("B: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       B_s, B_f1, B_f0, B_e, B_c, B_f1, B_f0, B_e + 1023);
#endif

	FP_MUL_D(R, A, B);

#ifdef DEBUG
	printk("D: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       R_s, R_f1, R_f0, R_e, R_c, R_f1, R_f0, R_e + 1023);
#endif

	return (ret | __FP_PACK_D(rD, R));
}
