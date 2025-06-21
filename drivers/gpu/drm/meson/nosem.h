/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */

#ifndef __NOSEM_H
#define __NOSEM_H

#define UPDATE_CFG      0x04
#define UPDATE_SCL      0x02
#define UPDATE_FB       0x01
#define UPDATE_ALL      (UPDATE_CFG | UPDATE_SCL | UPDATE_FB)
#define UPDATE_NONE     0x00

#define NOSEM_IDLE				(10*1000*1000*1000ULL)
#define NOSEM_WAIT_TIMEOUT		50

#endif /* __NOSEM_H */
