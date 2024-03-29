/***************************************************************************
 *     Copyright (c) 1999-2010, Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Module Description:
 *                     DO NOT EDIT THIS FILE DIRECTLY
 *
 * This module was generated magically with RDB from a source description
 * file. You must edit the source file for changes to be made to this file.
 *
 *
 * Date:           Generated on         Wed Aug 18 11:00:53 2010
 *                 MD5 Checksum         1af347fab7c27212d0dc8fd98a206da1
 *
 * Compiled with:  RDB Utility          combo_header.pl
 *                 RDB Parser           3.0
 *                 unknown              unknown
 *                 Perl Interpreter     5.008008
 *                 Operating System     linux
 *
 * Revision History:
 *
 * $brcm_Log: /magnum/basemodules/chp/7425/rdb/a0/bchp_ddr40_phy_control_regs_1.h $
 * 
 * Hydra_Software_Devel/1   8/18/10 4:54p tdo
 * SW7425-6: Create scripts and checkin initial version of RDB header
 * files
 *
 ***************************************************************************/

#ifndef BCHP_DDR40_PHY_CONTROL_REGS_1_H__
#define BCHP_DDR40_PHY_CONTROL_REGS_1_H__

/***************************************************************************
 *DDR40_PHY_CONTROL_REGS_1 - DDR40 DDR40 physical interface control registers 1
 ***************************************************************************/
#define BCHP_DDR40_PHY_CONTROL_REGS_1_REVISION   0x003c6000 /* Address & Control revision register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_CLK_PM_CTRL 0x003c6004 /* PHY clock power management control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PLL_STATUS 0x003c6010 /* PHY PLL status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PLL_CONFIG 0x003c6014 /* PHY PLL configuration register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PLL_CONTROL 0x003c6018 /* PHY PLL control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PLL_DIVIDERS 0x003c601c /* PHY PLL dividers control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_AUX_CONTROL 0x003c6020 /* Aux Control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_OVRIDE_BYTE_CTL 0x003c6030 /* Address & Control coarse VDL static override control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_OVRIDE_BIT_CTL 0x003c6034 /* Address & Control fine VDL static override control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_IDLE_PAD_CONTROL 0x003c6038 /* Idle mode SSTL pad control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_ZQ_PVT_COMP_CTL 0x003c603c /* PVT Compensation control and status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_DRIVE_PAD_CTL 0x003c6040 /* SSTL pad drive characteristics control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_CALIBRATE 0x003c6048 /* PHY VDL calibration control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_CALIB_STATUS 0x003c604c /* PHY VDL calibration status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_DQ_CALIB_STATUS 0x003c6050 /* PHY DQ VDL calibration status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_WR_CHAN_CALIB_STATUS 0x003c6054 /* PHY Write Channel VDL calibration status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VDL_RD_EN_CALIB_STATUS 0x003c6058 /* PHY Read Enable VDL calibration status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VIRTUAL_VTT_CONTROL 0x003c605c /* Virtual VTT Control and Status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VIRTUAL_VTT_STATUS 0x003c6060 /* Virtual VTT Control and Status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VIRTUAL_VTT_CONNECTIONS 0x003c6064 /* Virtual VTT Connections register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VIRTUAL_VTT_OVERRIDE 0x003c6068 /* Virtual VTT Override register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_VREF_DAC_CONTROL 0x003c606c /* VREF DAC Control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_CNTRL 0x003c6070 /* PhyBist Control Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_SEED 0x003c6074 /* PhyBist Seed Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_STATUS 0x003c6078 /* PhyBist General Status Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_CTL_STATUS 0x003c607c /* PhyBist Per-Bit Control Pad Status Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_DQ_STATUS 0x003c6080 /* PhyBist Per-Bit DQ Pad Status Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_PHYBIST_MISC_STATUS 0x003c6084 /* PhyBist Per-Bit DM and CK Pad Status Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_COMMAND_REG 0x003c6090 /* DRAM Command Register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_MODE_REG0  0x003c6094 /* Mode Register 0 */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_MODE_REG1  0x003c6098 /* Mode Register 1 */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_MODE_REG2  0x003c609c /* Mode Register 2 */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_MODE_REG3  0x003c60a0 /* Mode Register 3 */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_STANDBY_CONTROL 0x003c60a4 /* Standby Control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_STRAP_CONTROL 0x003c60b0 /* Strap Control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_STRAP_CONTROL2 0x003c60b4 /* Strap Control register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_STRAP_STATUS 0x003c60b8 /* Strap Status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_STRAP_STATUS2 0x003c60bc /* Strap Status register */
#define BCHP_DDR40_PHY_CONTROL_REGS_1_DEBUG_FREEZE_ENABLE 0x003c60c0 /* Freeze-on-error enable register */

#endif /* #ifndef BCHP_DDR40_PHY_CONTROL_REGS_1_H__ */

/* End of File */
