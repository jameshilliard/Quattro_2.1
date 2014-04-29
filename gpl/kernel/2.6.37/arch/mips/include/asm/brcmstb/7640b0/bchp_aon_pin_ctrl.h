/***************************************************************************
 *     Copyright (c) 1999-2011, Broadcom Corporation
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
 * Date:           Generated on         Thu Jun 23 16:46:57 2011
 *                 MD5 Checksum         5a01cf5675903503ea6423a69b7413f7
 *
 * Compiled with:  RDB Utility          combo_header.pl
 *                 RDB Parser           3.0
 *                 unknown              unknown
 *                 Perl Interpreter     5.008008
 *                 Operating System     linux
 *
 * Revision History:
 *
 * $brcm_Log: /magnum/basemodules/chp/7640/rdb/b0/bchp_aon_pin_ctrl.h $
 * 
 * Hydra_Software_Devel/2   6/24/11 4:30p etrudeau
 * SWBLURAY-25888: update 7640 B0 RDB with latest in Central RDB
 * 
 * Hydra_Software_Devel/2   6/24/11 4:30p etrudeau
 * SWBLURAY-25888: update 7640 B0 RDB with latest in Central RDB
 *
 ***************************************************************************/

#ifndef BCHP_AON_PIN_CTRL_H__
#define BCHP_AON_PIN_CTRL_H__

/***************************************************************************
 *AON_PIN_CTRL - AON Pinmux Control Registers
 ***************************************************************************/
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0         0x00440500 /* Pinmux control register 0 */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1         0x00440504 /* Pinmux control register 1 */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2         0x00440508 /* Pinmux control register 2 */
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0     0x0044050c /* Bypass clock unselect register 0 */

/***************************************************************************
 *PIN_MUX_CTRL_0 - Pinmux control register 0
 ***************************************************************************/
/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_gpio_04 [31:28] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_MASK          0xf0000000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_SHIFT         28
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_AON_GPIO_04   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_TVM_GPIO_04   1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_04_TP_OUT_17     2

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_gpio_03 [27:24] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_MASK          0x0f000000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_SHIFT         24
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_AON_GPIO_03   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_AON_UART_RX   1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_TVM_UART_RX   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_TVM_GPIO_03   3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_03_TP_OUT_16     4

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_gpio_02 [23:20] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_MASK          0x00f00000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_SHIFT         20
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_AON_GPIO_02   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_AON_UART_TX   1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_TVM_UART_TX   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_TVM_GPIO_02   3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_02_TP_OUT_15     4

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_gpio_01 [19:16] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_MASK          0x000f0000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_SHIFT         16
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_AON_GPIO_01   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_IR_IN         1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_01_TVM_GPIO_01   2

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_gpio_00 [15:12] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_MASK          0x0000f000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_SHIFT         12
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_AON_GPIO_00   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_FP_4SEC_RESETB 1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_gpio_00_TVM_GPIO_00   2

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_hdmi_rx_htplg_out [11:08] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_htplg_out_MASK 0x00000f00
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_htplg_out_SHIFT 8
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_htplg_out_DEFAULT 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_htplg_out_AON_HDMI_RX_HTPLG_OUT 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_htplg_out_TVM_HDMI_RX_HTPLG_OUT 1

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_hdmi_rx_cec [07:04] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_cec_MASK      0x000000f0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_cec_SHIFT     4
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_cec_DEFAULT   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_cec_AON_HDMI_RX_CEC 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_rx_cec_TVM_HDMI_RX_CEC 1

/* AON_PIN_CTRL :: PIN_MUX_CTRL_0 :: aon_hdmi_tx_cec [03:00] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_tx_cec_MASK      0x0000000f
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_tx_cec_SHIFT     0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_tx_cec_DEFAULT   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_tx_cec_AON_HDMI_TX_CEC 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_0_aon_hdmi_tx_cec_TVM_HDMI_TX_CEC 1

/***************************************************************************
 *PIN_MUX_CTRL_1 - Pinmux control register 1
 ***************************************************************************/
/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_sgpio_03 [31:28] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_03_MASK         0xf0000000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_03_SHIFT        28
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_03_DEFAULT      0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_03_AON_SGPIO_03 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_03_BSC_M_SDA1   1

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_sgpio_02 [27:24] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_02_MASK         0x0f000000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_02_SHIFT        24
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_02_DEFAULT      0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_02_AON_SGPIO_02 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_02_BSC_M_SCL1   1

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_sgpio_01 [23:20] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_MASK         0x00f00000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_SHIFT        20
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_DEFAULT      0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_AON_SGPIO_01 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_BSC_M_SDA0   1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_01_TVM_I2C_SDA  2

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_sgpio_00 [19:16] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_MASK         0x000f0000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_SHIFT        16
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_DEFAULT      0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_AON_SGPIO_00 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_BSC_M_SCL0   1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_sgpio_00_TVM_I2C_SCL  2

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_gpio_08 [15:12] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_MASK          0x0000f000
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_SHIFT         12
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_AON_GPIO_08   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_SPI_M_SS0     1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_TVM_GPIO_08   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_TVM_SPI_M_SS0 3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_08_TP_OUT_21     4

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_gpio_07 [11:08] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_MASK          0x00000f00
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_SHIFT         8
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_AON_GPIO_07   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_SPI_M_MOSI    1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_AON_UART_TX   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_TVM_GPIO_07   3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_TVM_SPI_M_MOSI 4
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_TVM_UART_TX   5
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_07_TP_OUT_20     6

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_gpio_06 [07:04] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_MASK          0x000000f0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_SHIFT         4
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_AON_GPIO_06   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_SPI_M_MISO    1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_AON_UART_RX   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_TVM_GPIO_06   3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_TVM_SPI_M_MISO 4
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_TVM_UART_RX   5
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_06_TP_OUT_19     6

/* AON_PIN_CTRL :: PIN_MUX_CTRL_1 :: aon_gpio_05 [03:00] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_MASK          0x0000000f
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_SHIFT         0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_DEFAULT       0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_AON_GPIO_05   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_SPI_M_SCLK    1
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_TVM_GPIO_05   2
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_TVM_SPI_M_SCLK 3
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_1_aon_gpio_05_TP_OUT_18     4

/***************************************************************************
 *PIN_MUX_CTRL_2 - Pinmux control register 2
 ***************************************************************************/
/* AON_PIN_CTRL :: PIN_MUX_CTRL_2 :: reserved0 [31:04] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_reserved0_MASK            0xfffffff0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_reserved0_SHIFT           4

/* AON_PIN_CTRL :: PIN_MUX_CTRL_2 :: aon_hdmi_s_sda_rx [03:00] */
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_aon_hdmi_s_sda_rx_MASK    0x0000000f
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_aon_hdmi_s_sda_rx_SHIFT   0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_aon_hdmi_s_sda_rx_DEFAULT 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_aon_hdmi_s_sda_rx_AON_HDMI_S_SDA_RX 0
#define BCHP_AON_PIN_CTRL_PIN_MUX_CTRL_2_aon_hdmi_s_sda_rx_TVM_HDMI_S_SDA_RX 1

/***************************************************************************
 *BYP_CLK_UNSELECT_0 - Bypass clock unselect register 0
 ***************************************************************************/
/* AON_PIN_CTRL :: BYP_CLK_UNSELECT_0 :: reserved0 [31:01] */
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0_reserved0_MASK        0xfffffffe
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0_reserved0_SHIFT       1

/* AON_PIN_CTRL :: BYP_CLK_UNSELECT_0 :: unsel_byp_clk_on_aon_sgpio_02 [00:00] */
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0_unsel_byp_clk_on_aon_sgpio_02_MASK 0x00000001
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0_unsel_byp_clk_on_aon_sgpio_02_SHIFT 0
#define BCHP_AON_PIN_CTRL_BYP_CLK_UNSELECT_0_unsel_byp_clk_on_aon_sgpio_02_DEFAULT 0

#endif /* #ifndef BCHP_AON_PIN_CTRL_H__ */

/* End of File */