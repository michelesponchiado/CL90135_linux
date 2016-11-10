/*
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * Copyright (C) 2008 MontaVista, Software, Inc.
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef __ARCH_ARM_DA8XX_DA8XX_H
#define __ARCH_ARM_DA8XX_DA8XX_H

void da8xx_map_common_io(void);
void da8xx_init_common_hw(void);
void da8xx_check_revision(void);
int da8xx_clk_init(void);

#endif /* __ARCH_ARM_DA8XX_DA8XX_H */
