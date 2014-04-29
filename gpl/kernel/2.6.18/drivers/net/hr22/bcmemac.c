/*
 *
 * Copyright (c) 2002-2005 Broadcom Corporation 
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
 *
*/
//**************************************************************************
// File Name  : bcmemac.c
//
// Description: This is Linux network driver for the BCM 
// 				EMAC Internal Ethenet Controller onboard the 7110
//               
// Updates    : 09/26/2001  jefchiao.  Created.
//**************************************************************************

#define CARDNAME    "BCMINTMAC"
#define VERSION     "2.0"
#define VER_STR     "v" VERSION " " __DATE__ " " __TIME__

// Turn this on to allow Hardware Multicast Filter
#define MULTICAST_HW_FILTER

#if defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
   #include <linux/modversions.h> 
   #define MODVERSIONS
#endif  

#define MOD_DEC_USE_COUNT
#define MOD_INC_USE_COUNT

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#if LINUX_VERSION_CODE >= 0x020411      /* 2.4.17 */
#include <linux/slab.h>
#else
#include <linux/malloc.h>
#endif
#include <linux/string.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/mipsregs.h>

#if 1
/* 5/24/06: Regardless of flash size, the offset of the MAC-addr is always the same */
#ifdef CONFIG_MTD_THOMSON
#define FLASH_MACADDR_ADDR 0xBF03F8F0  // Assumes inversion off at this point.
#else
#define FLASH_MACADDR_ADDR 0xBFFFF824
#endif
#else
	  
  #ifdef CONFIG_MIPS_BCM7318
  #include <asm/brcmstb/common/brcmstb.h>
  /* 16MB Flash */
  #define FLASH_MACADDR_OFFSET 0x00FFF824
  
  #elif defined(CONFIG_MIPS_BCM7038) || defined(CONFIG_MIPS_BCM7400) || \
        defined(CONFIG_MIPS_BCM7401) || defined(CONFIG_MIPS_BCM7403) || \
        defined(CONFIG_MIPS_BCM7452)
  /* 32MB Flash */
  #define FLASH_MACADDR_OFFSET 0x01FFF824

  #else
  #error "Unknown platform, FLASH_MACADDR_OFFSET must be defined\n"
  #endif

#endif

#include "board_setup.h"
#include "bcmmii.h"
#include "bcmemac.h"

#include <linux/stddef.h>

#ifdef USE_PROC
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#define PROC_ENTRY_NAME     "driver/ethinfo"
#endif

extern int init_ethsw_pinmux(BcmEnet_devctrl * pDevCtrl);
extern int get_phy_port_config(int phy_port_t[], int num_of_ports);

extern void bcm_inv_rac_all(void);
extern unsigned long getPhysFlashBase(void);
#ifdef VPORTS
extern int vnet_probe(void);
extern void vnet_module_cleanup(void);
#endif

static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr);
static int bcmemac_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int bcmIsEnetUp(struct net_device *dev);

#define POLLTIME_100MS      (HZ/10)
#define POLLCNT_1SEC        (HZ/POLLTIME_100MS)
#define POLLCNT_FOREVER     ((int) 0x80000000)

#define EMAC_TX_WATERMARK   0x180
#define VLAN_DISABLE        0
#define VLAN_ENABLE         1

#define MAKE4(x)   ((x[3] & 0xFF) | ((x[2] & 0xFF) << 8) | ((x[1] & 0xFF) << 16) | ((x[0] & 0xFF) << 24))
#define MAKE2(x)   ((x[1] & 0xFF) | ((x[0] & 0xFF) << 8))

#if LINUX_VERSION_CODE >= 0x020405      /* starting from 2.4.5 */
#define skb_dataref(x)   (&skb_shinfo(x)->dataref)
#else
#define skb_dataref(x)   skb_datarefp(x)
#endif


/*
 * IP Header Optimization, on 7401B0 and on-wards
 * We present an aligned buffer to the DMA engine, but ask it
 * to transfer the data with a 2-byte offset from the top.
 * The idea is to have the IP payload aligned on a 4-byte boundary
 * because the IP payload follows a 14-byte Ethernet header
 */

/*
 * Private fields for 7401B0 on-wards
 * Set the offset into the data buffer (bits 9-10 of RX_CONTROL)
 */
#define RX_CONFIG_PKT_OFFSET_SHIFT	9
#define RX_CONFIG_PKT_OFFSET_MASK		0x0000_0600


// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
//      External, indirect entry points. 
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
//      Called for "ifconfig ethX up" & "down"
// --------------------------------------------------------------------------
static int bcmemac_net_open(struct net_device * dev);
static int bcmemac_net_close(struct net_device * dev);
// --------------------------------------------------------------------------
//      Watchdog timeout
// --------------------------------------------------------------------------
static void bcmemac_net_timeout(struct net_device * dev);
// --------------------------------------------------------------------------
//      Packet transmission. 
// --------------------------------------------------------------------------
static int bcmemac_net_xmit(struct sk_buff * skb, struct net_device * dev);
// --------------------------------------------------------------------------
//      Allow proc filesystem to query driver statistics
// --------------------------------------------------------------------------
static struct net_device_stats * bcmemac_net_query(struct net_device * dev);
// --------------------------------------------------------------------------
//      Set address filtering mode
// --------------------------------------------------------------------------
static void bcm_set_multicast_list(struct net_device * dev);
// --------------------------------------------------------------------------
//      Set the hardware MAC address.
// --------------------------------------------------------------------------
static int bcm_set_mac_addr(struct net_device *dev, void *p);

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
//      Interrupt routines
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
static irqreturn_t bcmemac_net_isr(int irq, void *, struct pt_regs *regs);
// --------------------------------------------------------------------------
//  Bottom half service routine. Process all received packets.                  
// --------------------------------------------------------------------------
static void bcmemac_rx(void *);

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
//      Internal routines
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
/* Add an address to the ARL register */
static void write_mac_address(struct net_device *dev);
/* Remove registered netdevice */
static void bcmemac_init_cleanup(struct net_device *dev);
/* Allocate and initialize tx/rx buffer descriptor pools */
static int bcmemac_init_dev(BcmEnet_devctrl *pDevCtrl);
static int bcmemac_uninit_dev(BcmEnet_devctrl *pDevCtrl);
/* Assign the Rx descriptor ring */
static int assign_rx_buffers(BcmEnet_devctrl *pDevCtrl);
/* Initialize driver's pools of receive buffers and tranmit headers */
static int init_buffers(BcmEnet_devctrl *pDevCtrl);
/* Initialise the Ethernet Switch control registers */
static int init_emac(BcmEnet_devctrl *pDevCtrl);
/* Initialize the Ethernet Switch MIB registers */
static void clear_mib(volatile EmacRegisters *emac);

/* update an address to the EMAC perfect match registers */
static void perfectmatch_update(BcmEnet_devctrl *pDevCtrl, const uint8 * pAddr, bool bValid);

#ifdef MULTICAST_HW_FILTER
/* clear perfect match registers except given pAddr (PR10861) */
static void perfectmatch_clean(BcmEnet_devctrl *pDevCtrl, const uint8 * pAddr);
#endif

/* write an address to the EMAC perfect match registers */
static void perfectmatch_write(volatile EmacRegisters *emac, int reg, const uint8 * pAddr, bool bValid);
/* Initialize DMA control register */
static void init_IUdma(BcmEnet_devctrl *pDevCtrl);
/* Reclaim transmit frames which have been sent out */
static void tx_reclaim_timer(unsigned long arg);
/* Add a Tx control block to the pool */
static void txCb_enq(BcmEnet_devctrl *pDevCtrl, Enet_Tx_CB *txCbPtr);
/* Remove a Tx control block from the pool*/
static Enet_Tx_CB *txCb_deq(BcmEnet_devctrl *pDevCtrl);
static bool haveIPHdrOptimization(void);

#ifdef USE_PROC
static int dev_proc_engine(void *data, loff_t pos, char *buf);
static ssize_t eth_proc_read(struct file *file, char *buf, size_t count, loff_t *pos);
#endif

#ifdef DUMP_DATA
/* Display hex base data */
static void dumpHexData(unsigned char *head, int len);
/* dumpMem32 dump out the number of 32 bit hex data  */
static void dumpMem32(uint32 * pMemAddr, int iNumWords);
#endif

#ifdef CONFIG_BCMINTEMAC_NETLINK
static void bcmemac_link_change_task(BcmEnet_devctrl *pDevCtrl);
#endif

static struct net_device *eth_root_dev = NULL;
static struct net_device* vnet_dev[MAX_NUM_OF_VPORTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static struct net_device* dev_xmit_halt[MAX_NUM_OF_VPORTS] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
struct net_device *get_vnet_dev(int port);
struct net_device *set_vnet_dev(int port, struct net_device *dev);
int get_num_vports(void);

EXPORT_SYMBOL(get_num_vports);
EXPORT_SYMBOL(get_vnet_dev);
EXPORT_SYMBOL(set_vnet_dev);

int get_num_vports(void)
{
    BcmEnet_devctrl *pDevCtrl;

	if (vnet_dev[0] == NULL)
		return 0;

	pDevCtrl = (BcmEnet_devctrl*)netdev_priv(vnet_dev[0]);
	return pDevCtrl->EnetInfo.numSwitchPorts + 1;
}

struct net_device* get_vnet_dev(int port)
{
    if ((port < 0) || (port >= MAX_NUM_OF_VPORTS))
        return NULL;

    return vnet_dev[port];
}

struct net_device *set_vnet_dev(int port, struct net_device *dev)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(eth_root_dev);
    unsigned long flags;
#if defined(CONFIG_PHY_PORT_MAPPING)
    int i, count = 0;
#endif

    if ((port < 0) || (port >= MAX_NUM_OF_VPORTS))
        return NULL;

    spin_lock_irqsave(&pDevCtrl->lock, flags);

    if (port == 1)
    {
        if (dev != NULL)
            ethsw_switch_frame_manage_mode(vnet_dev[0]);
        else
            ethsw_switch_unmanage_mode(vnet_dev[0]);
    }

    vnet_dev[port] = dev;
#if defined(CONFIG_PHY_PORT_MAPPING)
    for (i = 1; i <= sizeof(pDevCtrl->phy_port)/sizeof(int); i++)
    {
        if (pDevCtrl->phy_port[i - 1] != 0) 
        {
            count++;
            if (count == port)
            {
                i = i << PHY_PORT;
                dev->base_addr |= i;
            }
        }
    }
#endif
    spin_unlock_irqrestore(&pDevCtrl->lock, flags);
    return dev;
}

#ifdef VPORTS
static inline struct sk_buff *bcmemac_put_tag(struct sk_buff *skb,struct net_device *dev);
static int bcmemac_header(struct sk_buff *s, struct net_device *d, unsigned short ty, void *da, void *sa, unsigned len);
#define is_vport(dev) ((vnet_dev[0] != NULL) && (dev->hard_header == vnet_dev[0]->hard_header))
#define is_vmode() (vnet_dev[1] != NULL)
#if defined(CONFIG_PHY_PORT_MAPPING)
#define egress_vport_id_from_dev(dev) ((dev->base_addr == vnet_dev[0]->base_addr) ? 0: ((dev->base_addr >> PHY_PORT) - 1))
#else
#define egress_vport_id_from_dev(dev) ((dev->base_addr == vnet_dev[0]->base_addr) ? 0: (dev->base_addr - 1))
#endif
#define ingress_vport_id_from_tag(p) (((unsigned char *)p)[17] & 0x0f)
#endif

/* --------------------------------------------------------------------------
    Name: bcmemac_get_free_txdesc
 Purpose: Get Current Available TX desc count
-------------------------------------------------------------------------- */
int bcmemac_get_free_txdesc( struct net_device *dev ){
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
    return pDevCtrl->txFreeBds;
}
/* --------------------------------------------------------------------------
    Name: bcmemac_get_device
 Purpose: Obtain net_device handle (for export) 
-------------------------------------------------------------------------- */
struct net_device * bcmemac_get_device( void ){
    return eth_root_dev;
}

static inline void IncFlowCtrlAllocRegister(BcmEnet_devctrl *pDevCtrl) 
{
    volatile unsigned long* pAllocReg = &pDevCtrl->dmaRegs->flowctl_ch1_alloc;

    /* Make sure we don't go over bound */
    if (*pAllocReg < NR_RX_BDS) {
        *pAllocReg = 1;
    }
}

static bool haveIPHdrOptimization(void)
{
    bool bhaveIPHeaderOpt;

    // Which revisions of the chip have the fix.
#ifdef CONFIG_MIPS_BCM7438
#define FIXED_REV	0x74380010	/* FIXME */
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;
#elif defined CONFIG_MIPS_BCM7038
    #define FIXED_REV	0x70380025	//FIXME BCM7038C5?
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;
#elif defined( CONFIG_MIPS_BCM7318 )
    #define FIXED_REV	0x73180013	/****** 7318B3? *********/
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xfffe0600;
#elif defined( CONFIG_MIPS_BCM7400 ) 
    #define FIXED_REV	0x74000001	/****** FIX ME *********/
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;
#elif defined( CONFIG_MIPS_BCM7401 ) || defined( CONFIG_MIPS_BCM7402 ) || \
      defined( CONFIG_MIPS_BCM7402S )
    #define FIXED_REV   0x74010010      /****** 7401B0 is first chip with the fix *********/
                                    /* Should also work with a true 7402 chip */
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;
#elif defined(CONFIG_MIPS_BCM7403 ) || defined(CONFIG_MIPS_BCM7452)
    #define FIXED_REV       0x74010010
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;

#elif defined( CONFIG_MIPS_BCM7440 )
    #define FIXED_REV   0x0
    volatile unsigned long* pSundryRev = (volatile unsigned long*) 0xb0404000;
#else
    #error "Unsupported platform"
#endif

    bhaveIPHeaderOpt = (*pSundryRev >= FIXED_REV);
    printk("SUNDRY revision = %08lx, have IP Hdr Opt=%d\n", *pSundryRev, bhaveIPHeaderOpt? 1 : 0);

    return bhaveIPHeaderOpt;
}

#ifdef DUMP_DATA
/*
 * dumpHexData dump out the hex base binary data
 */
static void dumpHexData(unsigned char *head, int len)
{
    int i;
    unsigned char *curPtr = head;

    for (i = 0; i < len; ++i)
    {
        if (i % 16 == 0)
            printk("\n");       
        printk("0x%02X, ", *curPtr++);
    }
    printk("\n");

}

/*
 * dumpMem32 dump out the number of 32 bit hex data 
 */
static void dumpMem32(uint32 * pMemAddr, int iNumWords)
{
    int i = 0;
    static char buffer[80];

    sprintf(buffer, "%08X: ", (UINT)pMemAddr);
    printk(buffer);
    while (iNumWords) {
        sprintf(buffer, "%08X ", (UINT)*pMemAddr++);
        printk(buffer);
        iNumWords--;
        i++;
        if ((i % 4) == 0 && iNumWords) {
            sprintf(buffer, "\n%08X: ", (UINT)pMemAddr);
            printk(buffer);
        }
    }
    printk("\n");
}
#endif

/* --------------------------------------------------------------------------
    Name: bcmemac_net_open
 Purpose: Open and Initialize the EMAC on the chip
-------------------------------------------------------------------------- */
static int bcmemac_net_open(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    ASSERT(pDevCtrl != NULL);

    TRACE(("%s: bcmemac_net_open, EMACConf=%x, &RxDMA=%x, rxDma.cfg=%x\n", 
                dev->name, pDevCtrl->emac->config, &pDevCtrl->rxDma, pDevCtrl->rxDma->cfg));

#ifdef VPORTS
    if (is_vport(dev) && (dev != vnet_dev[0]))
    {
        if ((vnet_dev[0]->flags & IFF_UP) == 0) {
            return -ENETDOWN;
        }

        netif_start_queue(dev);
        return 0;
    }
#endif

    MOD_INC_USE_COUNT;

    /* disable ethernet MAC while updating its registers */
    pDevCtrl->emac->config |= EMAC_DISABLE ;
    while(pDevCtrl->emac->config & EMAC_DISABLE);

    pDevCtrl->emac->config |= EMAC_ENABLE;  
    pDevCtrl->dmaRegs->controller_cfg |= IUDMA_ENABLE;         

    // Tell Tx DMA engine to start from the top
#ifdef IUDMA_INIT_WORKAROUND
    {
        unsigned long diag_rdbk;

        pDevCtrl->dmaRegs->enet_iudma_diag_ctl = 0x100; /* enable to read diags. */
        diag_rdbk = pDevCtrl->dmaRegs->enet_iudma_diag_rdbk;
        pDevCtrl->txDma->descPtr = (uint32)CPHYSADDR(pDevCtrl->txFirstBdPtr);
        pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr + ((diag_rdbk>>(16+1))&DESC_MASK);
    }
#endif
	
    /* Initialize emac registers */
    if (pDevCtrl->bIPHdrOptimize) {
        pDevCtrl->emac->rxControl = EMAC_FC_EN | EMAC_UNIFLOW  | (2<<RX_CONFIG_PKT_OFFSET_SHIFT);  
    } 
    else {
        pDevCtrl->emac->rxControl = EMAC_FC_EN | EMAC_UNIFLOW /* |EMAC_PROM*/ ;   // Don't allow Promiscuous
    }
    pDevCtrl->rxDma->cfg |= DMA_ENABLE;

    atomic_inc(&pDevCtrl->devInUsed);

    /* Are we previously closed by bcmemac_net_close, which set InUsed to -1 */
    if (0 == atomic_read(&pDevCtrl->devInUsed)) {
        atomic_inc(&pDevCtrl->devInUsed);
        enable_irq(pDevCtrl->rxIrq);
    }

#ifdef CONFIG_TIVO
#ifdef CONFIG_BCMINTEMAC_NETLINK
    /* Match the initial linkState */
    netif_carrier_off(pDevCtrl->dev);
#endif
#endif /* CONFIG_TIVO */

#ifdef CONFIG_SMP
    if (smp_processor_id() == 0) {
#endif    
        pDevCtrl->timer.expires = jiffies + POLLTIME_100MS;
        add_timer(&pDevCtrl->timer);

#ifdef CONFIG_SMP
    }
#endif    
    // Start the network engine
    netif_start_queue(dev);

    return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void bcmemac_poll (struct net_device *dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
	/* disable_irq is not very nice, but with the funny lockless design
	   we have no other choice. */
    /* Run TX Queue */
    bcmemac_net_xmit(NULL, pDevCtrl->dev);
    /* Run RX Queue */
    bcmemac_rx(pDevCtrl);
    /* Check for Interrupts */
    bcmemac_net_isr (dev->irq, dev, NULL);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/* --------------------------------------------------------------------------
    Name: bcmemac_net_close
 Purpose: Stop communicating with the outside world
    Note: Caused by 'ifconfig ethX down'
-------------------------------------------------------------------------- */
static int bcmemac_net_close(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    Enet_Tx_CB *txCBPtr;

    ASSERT(pDevCtrl != NULL);

    TRACE(("%s: bcmemac_net_close\n", dev->name));

#ifdef VPORTS
    if (is_vport(dev) && (dev != vnet_dev[0]))
    {
        netif_stop_queue(dev);
        return 0;
    }
#endif
    netif_stop_queue(dev);

    MOD_DEC_USE_COUNT;

    atomic_dec(&pDevCtrl->devInUsed);

    pDevCtrl->rxDma->cfg &= ~DMA_ENABLE;

    pDevCtrl->emac->config |= EMAC_DISABLE;           

    if (0 == atomic_read(&pDevCtrl->devInUsed)) {
        atomic_set(&pDevCtrl->devInUsed, -1); /* Mark interrupt disable */
        disable_irq(pDevCtrl->rxIrq);
    }

    del_timer_sync(&pDevCtrl->timer);

    /* free the skb in the txCbPtrHead */
    while (pDevCtrl->txCbPtrHead)  {
        pDevCtrl->txFreeBds += pDevCtrl->txCbPtrHead->nrBds;

        if(pDevCtrl->txCbPtrHead->skb)
            dev_kfree_skb (pDevCtrl->txCbPtrHead->skb);

        txCBPtr = pDevCtrl->txCbPtrHead;

        /* Advance the current reclaim pointer */
        pDevCtrl->txCbPtrHead = pDevCtrl->txCbPtrHead->next;

        /* Finally, return the transmit header to the free list */
        txCb_enq(pDevCtrl, txCBPtr);
    }
    /* Adjust the tail if the list is empty */
    if(pDevCtrl->txCbPtrHead == NULL)
        pDevCtrl->txCbPtrTail = NULL;

    pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr = pDevCtrl->txBds;
    return 0;
}

/* --------------------------------------------------------------------------
    Name: bcmemac_net_timeout
 Purpose: 
-------------------------------------------------------------------------- */
static void bcmemac_net_timeout(struct net_device * dev)
{
    ASSERT(dev != NULL);

    TRACE(("%s: bcmemac_net_timeout\n", dev->name));

    dev->trans_start = jiffies;

    netif_wake_queue(dev);
}

/* --------------------------------------------------------------------------
    Name: bcm_set_multicast_list
 Purpose: Set the multicast mode, ie. promiscuous or multicast
-------------------------------------------------------------------------- */
static void bcm_set_multicast_list(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
#ifdef VPORTS
    static int num_of_promisc_vports = 0;

    if (is_vport(dev))
        pDevCtrl = netdev_priv(vnet_dev[0]);
#endif

    TRACE(("%s: bcm_set_multicast_list: %08X\n", dev->name, dev->flags));

    /* Promiscous mode */
    if (dev->flags & IFF_PROMISC) {
#ifdef VPORTS
        if (is_vport(dev))
            num_of_promisc_vports ++;
#endif
        pDevCtrl->emac->rxControl |= EMAC_PROM;   
    } else {
#ifdef VPORTS
        if (is_vport(dev)) {
            if (-- num_of_promisc_vports == 0)
                pDevCtrl->emac->rxControl &= ~EMAC_PROM;
        } 
        else 
#endif
        {
            pDevCtrl->emac->rxControl &= ~EMAC_PROM;
        }
    }

#ifndef MULTICAST_HW_FILTER
    /* All Multicast packets (PR10861 Check for any multicast request) */
    if (dev->flags & IFF_ALLMULTI || dev->mc_count) {
        pDevCtrl->emac->rxControl |= EMAC_ALL_MCAST;
    } else {
        pDevCtrl->emac->rxControl &= ~EMAC_ALL_MCAST;
    }
#else
    {
        struct dev_mc_list *dmi = dev->mc_list;
        /* PR10861 - Filter specific group Multicast packets (R&C 2nd Ed., p463) */
        if (dev->flags & IFF_ALLMULTI  || dev->mc_count>(MAX_PMADDR-1)) {
            perfectmatch_clean((BcmEnet_devctrl *)pDevCtrl->dev->priv, dev->dev_addr);
            pDevCtrl->emac->rxControl |= EMAC_ALL_MCAST;
            return;
        } else {
            pDevCtrl->emac->rxControl &= ~EMAC_ALL_MCAST;
        }

        /* No multicast? Just get our own stuff */
        if (dev->mc_count == 0) 
            return;

        /* Store multicast addresses in the prefect match registers */
        perfectmatch_clean((BcmEnet_devctrl *)pDevCtrl->dev->priv, dev->dev_addr);
        for(dmi = dev->mc_list;	dmi ; dmi = dmi->next)
            perfectmatch_update((BcmEnet_devctrl *)pDevCtrl->dev->priv, dmi->dmi_addr, 1);
    }
#endif
}

/* --------------------------------------------------------------------------
    Name: bcmemac_net_query
 Purpose: Return the current statistics. This may be called with the card 
          open or closed.
-------------------------------------------------------------------------- */
static struct net_device_stats *
bcmemac_net_query(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;

    ASSERT(pDevCtrl != NULL);

    return &(pDevCtrl->stats);
}

/*
 * tx_reclaim_timer: reclaim transmit frames which have been sent out
 */
static void tx_reclaim_timer(unsigned long arg)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)arg;
    int bdfilled;
    int linkState;

    if (atomic_read(&pDevCtrl->rxDmaRefill) != 0) {
        atomic_set(&pDevCtrl->rxDmaRefill, 0);
        /* assign packet buffers to all available Dma descriptors */
        bdfilled = assign_rx_buffers(pDevCtrl);
    }

    /* Reclaim transmit Frames which have been sent out */
    bcmemac_net_xmit(NULL, pDevCtrl->dev);

    pDevCtrl->linkstatus_polltimer++;
    if ( pDevCtrl->linkstatus_polltimer >= POLLCNT_1SEC ) {
        linkState = bcmIsEnetUp(pDevCtrl->dev);

        if (linkState != pDevCtrl->linkState) {
            if (linkState != 0) {
                if (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH) {
                    if (pDevCtrl->linkState == 0)
                    {
                        pDevCtrl->emac->txControl |= EMAC_FD;
                        pDevCtrl->rxDma->cfg |= DMA_ENABLE;                    
                    }
                } else {
                    unsigned long v = mii_read(pDevCtrl->dev, pDevCtrl->EnetInfo.ucPhyAddress, MII_AUX_CTRL_STATUS);
                    if( (v & MII_AUX_CTRL_STATUS_FULL_DUPLEX) != 0) {
                        pDevCtrl->emac->txControl |= EMAC_FD;
                        pDevCtrl->dmaRegs->flowctl_ch1_alloc = (IUDMA_CH1_FLOW_ALLOC_FORCE | 0);
                        pDevCtrl->dmaRegs->flowctl_ch1_alloc = NR_RX_BDS;
                        pDevCtrl->rxDma->cfg |= DMA_ENABLE;
                    }
                    else
                        pDevCtrl->emac->txControl &= ~EMAC_FD;
                }

#ifdef CONFIG_BCMINTEMAC_NETLINK
                if (pDevCtrl->linkState == 0) {
                    netif_carrier_on(pDevCtrl->dev);
                    schedule_work(&pDevCtrl->link_change_task);	
                    printk((KERN_CRIT "%s Link UP.\n"),pDevCtrl->dev->name);
                }
#else
                if (pDevCtrl->linkState == 0) {
                    printk((KERN_CRIT "%s Link UP.\n"),pDevCtrl->dev->name);
                }
#endif
            } else {
#ifdef CONFIG_BCMINTEMAC_NETLINK
                if (pDevCtrl->linkState != 0) {
                    netif_carrier_off(pDevCtrl->dev);
                    schedule_work(&pDevCtrl->link_change_task);
                    printk((KERN_CRIT "%s Link DOWN.\n"),pDevCtrl->dev->name);
                }
#else
                if (pDevCtrl->linkState != 0)
                {
                    pDevCtrl->rxDma->cfg &= ~DMA_ENABLE;
                    printk((KERN_CRIT "%s Link DOWN.\n"),pDevCtrl->dev->name);
                }
#endif
            }

            /* Wake up the user mode application that monitors link status. */
            pDevCtrl->linkState = linkState;
        }
    }

#ifdef CONFIG_SMP
    if (smp_processor_id() == 0) {
#endif    
        pDevCtrl->timer.expires = jiffies + POLLTIME_100MS;
        add_timer(&pDevCtrl->timer);
#ifdef CONFIG_SMP
    }
#endif    
}

#ifdef CONFIG_BCMINTEMAC_NETLINK
static void bcmemac_link_change_task(BcmEnet_devctrl *pDevCtrl)
{
	if(!pDevCtrl) {
		printk("pDevCtrl is NULL\n");
		return;
	}

	rtnl_lock();
	if (pDevCtrl->linkState) {
		pDevCtrl->dev->flags |= IFF_RUNNING;
		rtmsg_ifinfo(RTM_NEWLINK, pDevCtrl->dev, IFF_RUNNING);
	}
	else {
		clear_bit(__LINK_STATE_START, &pDevCtrl->dev->state);
		pDevCtrl->dev->flags &= ~IFF_RUNNING;
		rtmsg_ifinfo(RTM_NEWLINK, pDevCtrl->dev, IFF_RUNNING);
		set_bit(__LINK_STATE_START, &pDevCtrl->dev->state);
	}
	rtnl_unlock();
}
#endif

/*
 * txCb_enq: add a Tx control block to the pool
 */
static void txCb_enq(BcmEnet_devctrl *pDevCtrl, Enet_Tx_CB *txCbPtr)
{
    txCbPtr->next = NULL;

    if (pDevCtrl->txCbQTail) {
        pDevCtrl->txCbQTail->next = txCbPtr;
        pDevCtrl->txCbQTail = txCbPtr;
    }
    else {
        pDevCtrl->txCbQHead = pDevCtrl->txCbQTail = txCbPtr;
    }
}

/*
 * txCb_deq: remove a Tx control block from the pool
 */
static Enet_Tx_CB *txCb_deq(BcmEnet_devctrl *pDevCtrl)
{
    Enet_Tx_CB *txCbPtr;

    if ((txCbPtr = pDevCtrl->txCbQHead)) {
        pDevCtrl->txCbQHead = txCbPtr->next;
        txCbPtr->next = NULL;

        if (pDevCtrl->txCbQHead == NULL)
            pDevCtrl->txCbQTail = NULL;
    }
    else {
        txCbPtr = NULL;
    }
    return txCbPtr;
}

/* --------------------------------------------------------------------------
    Name: bcmemac_xmit_check
 Purpose: Reclaims TX descriptors
-------------------------------------------------------------------------- */
int bcmemac_xmit_check(struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    Enet_Tx_CB *txCBPtr;
    Enet_Tx_CB *txedCBPtr;
    unsigned long flags,ret;

    ASSERT(pDevCtrl != NULL);

    /*
     * Obtain exclusive access to transmitter.  This is necessary because
     * we might have more than one stack transmitting at once.
     */
    spin_lock_irqsave(&pDevCtrl->lock, flags);
        
    txCBPtr = NULL;

    /* Reclaim transmitted buffers */
    while (pDevCtrl->txCbPtrHead)  {
        if (EMAC_SWAP32(pDevCtrl->txCbPtrHead->lastBdAddr->length_status) & (DMA_OWN)) {
            break;
        }
        pDevCtrl->txFreeBds += pDevCtrl->txCbPtrHead->nrBds;

        if(pDevCtrl->txCbPtrHead->skb) {
            dev_kfree_skb_any (pDevCtrl->txCbPtrHead->skb);
        }

        txedCBPtr = pDevCtrl->txCbPtrHead;

        /* Advance the current reclaim pointer */
        pDevCtrl->txCbPtrHead = pDevCtrl->txCbPtrHead->next;

        /* 
         * Finally, return the transmit header to the free list.
         * But keep one around, so we don't have to allocate again
         */
        txCb_enq(pDevCtrl, txedCBPtr);
    }

    /* Adjust the tail if the list is empty */
    if(pDevCtrl->txCbPtrHead == NULL)
        pDevCtrl->txCbPtrTail = NULL;

    netif_wake_queue(dev);

    if(pDevCtrl->txCbQHead && (pDevCtrl->txFreeBds>0))
        ret = 0;
    else
        ret = 1;

    spin_unlock_irqrestore(&pDevCtrl->lock, flags);
    return ret;
}

#ifdef VPORTS

static inline struct sk_buff *bcmemac_put_tag(struct sk_buff *skb,struct net_device *dev)
{
    BcmEnet_hdr *pHdr = (BcmEnet_hdr *)skb->data;
    int headroom;
    int tailroom;

    if (pHdr->brcm_type == BRCM_TYPE)
    {
        headroom = 0;
        tailroom = ETH_ZLEN + BRCM_TAG_LEN - skb->len;
    }
    else
    {
        headroom = BRCM_TAG_LEN;
        tailroom = ETH_ZLEN - skb->len;
    }

    tailroom = (tailroom < 0)? ETH_CRC_LEN: ETH_CRC_LEN + tailroom ;
    if ((skb_headroom(skb) < headroom) || (skb_tailroom(skb) < tailroom))
    {
        struct sk_buff *oskb = skb;

        skb = skb_copy_expand(oskb, headroom, tailroom, GFP_ATOMIC);
        kfree_skb(oskb);

        if (!skb)
            return NULL;
    }
    else if (headroom != 0)
    {
        skb = skb_unshare(skb, GFP_ATOMIC);

        if (!skb)
            return NULL;
    }

    memset(skb->data + skb->len, 0, tailroom);
    skb_put(skb, tailroom);

    if (headroom != 0)
    {
        BcmEnet_hdr *pHdr = (BcmEnet_hdr *)skb_push(skb, headroom);
        memmove(pHdr, skb->data + headroom, ETH_ALEN * 2);
        pHdr->brcm_type = BRCM_TYPE;
        pHdr->brcm_tag = 0;

        if (vnet_dev[1] != NULL)
            pHdr->brcm_tag |= BRCM_TAG_EGRESS | egress_vport_id_from_dev(dev);
    }
    return skb; 
}

static int bcmemac_header(struct sk_buff *skb, struct net_device *dev, unsigned short type, void *da, void *sa, unsigned len)
{
    struct ethhdr *eth;
    BcmEnet_hdr *pHdr;
    uint32 header_len;

    header_len = is_vmode() ? dev->hard_header_len: ETH_HLEN;
    eth = (struct ethhdr *)skb_push(skb, header_len);

    if (type != ETH_P_802_3)
        eth->h_proto = htons(type);
    else
        eth->h_proto = htons(len);

    if (!is_vmode())
        goto skip_tag;

    memmove(skb->data + 18, skb->data + 12, 2);

    pHdr = (BcmEnet_hdr *)eth;
    pHdr->brcm_type = BRCM_TYPE;
    pHdr->brcm_tag = 0;

    if (vnet_dev[1] != NULL)
        pHdr->brcm_tag |= BRCM_TAG_EGRESS | egress_vport_id_from_dev(dev);

skip_tag:

    if (sa)
        memcpy(eth->h_source, sa, dev->addr_len);
    else
        memcpy(eth->h_source, dev->dev_addr, dev->addr_len);

    if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
        memset(eth->h_dest, 0, dev->addr_len);
        return header_len;
    }

    if (da) {
        memcpy(eth->h_dest, da, dev->addr_len);
        return header_len;
    }
    return (-1 * header_len);
}

#endif

/* --------------------------------------------------------------------------
    Name: bcmemac_net_xmit
 Purpose: Send ethernet traffic
-------------------------------------------------------------------------- */
static int bcmemac_net_xmit(struct sk_buff * skb, struct net_device * dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    Enet_Tx_CB *txCBPtr;
    Enet_Tx_CB *txedCBPtr;
    volatile DmaDesc *firstBdPtr;
    unsigned long flags;
    int i = 0;

    ASSERT(pDevCtrl != NULL);

#ifdef VPORTS
    if (is_vport(dev))
        pDevCtrl = netdev_priv(vnet_dev[0]);
#endif
    /*
     * Obtain exclusive access to transmitter.  This is necessary because
     * we might have more than one stack transmitting at once.
     */
    spin_lock_irqsave(&pDevCtrl->lock, flags);
        
    txCBPtr = NULL;

    /* Reclaim transmitted buffers */
    while (pDevCtrl->txCbPtrHead)  {
        if (EMAC_SWAP32(pDevCtrl->txCbPtrHead->lastBdAddr->length_status) & DMA_OWN) {
            break;
        }

        pDevCtrl->txFreeBds += pDevCtrl->txCbPtrHead->nrBds;

        if(pDevCtrl->txCbPtrHead->skb) {
            dev_kfree_skb_any (pDevCtrl->txCbPtrHead->skb);
        }

        txedCBPtr = pDevCtrl->txCbPtrHead;

        /* Advance the current reclaim pointer */
        pDevCtrl->txCbPtrHead = pDevCtrl->txCbPtrHead->next;

        /* 
         * Finally, return the transmit header to the free list.
         * But keep one around, so we don't have to allocate again
         */
        if (txCBPtr == NULL) {
            txCBPtr = txedCBPtr;
        }
        else {
            txCb_enq(pDevCtrl, txedCBPtr);
        }
    }

    /* Adjust the tail if the list is empty */
    if(pDevCtrl->txCbPtrHead == NULL)
        pDevCtrl->txCbPtrTail = NULL;

    if (skb == NULL) {
        if (txCBPtr != NULL) {
            txCb_enq(pDevCtrl, txCBPtr);
        }
        while(dev_xmit_halt[i]) {
            netif_wake_queue(dev_xmit_halt[i]);
            dev_xmit_halt[i] = NULL;
            i++;
        }
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 0;
    }

    TRACE(("bcmemac_net_xmit, txCfg=%08x, txIntStat=%08x\n", pDevCtrl->txDma->cfg, pDevCtrl->txDma->intStat));
    if (txCBPtr == NULL) {
        txCBPtr = txCb_deq(pDevCtrl);
    }

    /*
     * Obtain a transmit header from the free list.  If there are none
     * available, we can't send the packet. Discard the packet.
     */
    if (txCBPtr == NULL) {
        while(dev_xmit_halt[i++] != NULL) {};
        netif_stop_queue(dev);
        dev_xmit_halt[--i] = dev;
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 1;
    }

#ifdef VPORTS
    if (is_vport(dev) && is_vmode())
        skb = bcmemac_put_tag(skb, dev);

    if (skb == NULL)
    {
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 0;
    }
#endif
    txCBPtr->nrBds = 1;
    txCBPtr->skb = skb;

    /* If we could not queue this packet, free it */
    if (pDevCtrl->txFreeBds < txCBPtr->nrBds) {
        TRACE(("%s: bcmemac_net_xmit low on txFreeBds\n", dev->name));
        txCb_enq(pDevCtrl, txCBPtr);
        while(dev_xmit_halt[i++] != NULL) {};
        netif_stop_queue(dev);
        dev_xmit_halt[--i] = dev;
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 1;
    }


    firstBdPtr = pDevCtrl->txNextBdPtr;
    /* store off the last BD address used to enqueue the packet */
    txCBPtr->lastBdAddr = pDevCtrl->txNextBdPtr;

    /* assign skb data to TX Dma descriptor */
    /*
     * Add the buffer to the ring.
     * Set addr and length of DMA BD to be transmitted.
     */
    dma_cache_wback_inv((unsigned long)skb->data, skb->len);

    pDevCtrl->txNextBdPtr->address = EMAC_SWAP32((uint32)CPHYSADDR(skb->data));
    pDevCtrl->txNextBdPtr->length_status  = EMAC_SWAP32((((unsigned long)((skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len))<<16));
    /*
     * Set status of DMA BD to be transmitted and
     * advance BD pointer to next in the chain.
     */
    if (pDevCtrl->txNextBdPtr == pDevCtrl->txLastBdPtr) {
        pDevCtrl->txNextBdPtr->length_status |= EMAC_SWAP32(DMA_WRAP);
        pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr;
    }
    else {
        pDevCtrl->txNextBdPtr->length_status |= EMAC_SWAP32(0);
        pDevCtrl->txNextBdPtr++;
    }
#ifdef DUMP_DATA
    printk("bcmemac_net_xmit: len %d", skb->len);
    dumpHexData(skb->data, skb->len);
#endif
    /*
     * Turn on the "OWN" bit in the first buffer descriptor
     * This tells the switch that it can transmit this frame.
     */
    firstBdPtr->length_status |= EMAC_SWAP32(DMA_OWN | DMA_SOP | DMA_EOP | DMA_APPEND_CRC);

    /* Decrement total BD count */
    pDevCtrl->txFreeBds -= txCBPtr->nrBds;

    if ( (pDevCtrl->txFreeBds == 0) || (pDevCtrl->txCbQHead == NULL) ) {
        TRACE(("%s: bcmemac_net_xmit no transmit queue space -- stopping queues\n", dev->name));
        while(dev_xmit_halt[i++] != NULL) {};
        netif_stop_queue(dev);
        dev_xmit_halt[--i] = dev;
    }
    /*
     * Packet was enqueued correctly.
     * Advance to the next Enet_Tx_CB needing to be assigned to a BD
     */
    txCBPtr->next = NULL;
    if(pDevCtrl->txCbPtrHead == NULL) {
        pDevCtrl->txCbPtrHead = txCBPtr;
        pDevCtrl->txCbPtrTail = txCBPtr;
    }
    else {
        pDevCtrl->txCbPtrTail->next = txCBPtr;
        pDevCtrl->txCbPtrTail = txCBPtr;
    }

    /* Enable DMA for this channel */
    pDevCtrl->txDma->cfg |= DMA_ENABLE;

    /* update stats */
#ifdef VPORTS
    if (is_vport(dev) && (dev->base_addr > 0) && (dev->base_addr <= pDevCtrl->EnetInfo.numSwitchPorts)) {
        struct net_device_stats *s =
            (struct net_device_stats *)netdev_priv(vnet_dev[dev->base_addr]);

        s->tx_bytes += ((skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len);
        s->tx_bytes += ETH_CRC_LEN;
        s->tx_packets++;
    }
#endif
    pDevCtrl->stats.tx_bytes += ((skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len);
    pDevCtrl->stats.tx_bytes += 4;
    pDevCtrl->stats.tx_packets++;

    dev->trans_start = jiffies;

    spin_unlock_irqrestore(&pDevCtrl->lock, flags);

    return 0;
}

/* --------------------------------------------------------------------------
    Name: bcmemac_xmit_fragment
 Purpose: Send ethernet traffic Buffer DESC and submit to UDMA
-------------------------------------------------------------------------- */
int bcmemac_xmit_fragment( int ch, unsigned char *buf, int buf_len, 
                           unsigned long tx_flags , struct net_device *dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    Enet_Tx_CB *txCBPtr;
    volatile DmaDesc *firstBdPtr;
    unsigned long flags;
	
    ASSERT(pDevCtrl != NULL);
    /*
     * Obtain exclusive access to transmitter.  This is necessary because
     * we might have more than one stack transmitting at once.
     */
    spin_lock_irqsave(&pDevCtrl->lock, flags);
                
    txCBPtr = txCb_deq(pDevCtrl);
    /*
     * Obtain a transmit header from the free list.  If there are none
     * available, we can't send the packet. Discard the packet.
     */
    if (txCBPtr == NULL) {
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 1;
    }

    txCBPtr->nrBds = 1;
    txCBPtr->skb = NULL;

    /* If we could not queue this packet, free it */
    if (pDevCtrl->txFreeBds < txCBPtr->nrBds) {
        TRACE(("%s: bcmemac_net_xmit low on txFreeBds\n", dev->name));
        txCb_enq(pDevCtrl, txCBPtr);
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        return 1;
    }

	/*--------first fragment------*/
    firstBdPtr = pDevCtrl->txNextBdPtr;
    /* store off the last BD address used to enqueue the packet */
    txCBPtr->lastBdAddr = pDevCtrl->txNextBdPtr;

    /* assign skb data to TX Dma descriptor */
    /*
     * Add the buffer to the ring.
     * Set addr and length of DMA BD to be transmitted.
     */
    pDevCtrl->txNextBdPtr->address = EMAC_SWAP32((uint32)CPHYSADDR(buf));
    pDevCtrl->txNextBdPtr->length_status  = EMAC_SWAP32((((unsigned long)buf_len)<<16));	
    /*
     * Set status of DMA BD to be transmitted and
     * advance BD pointer to next in the chain.
     */
    if (pDevCtrl->txNextBdPtr == pDevCtrl->txLastBdPtr) {
        pDevCtrl->txNextBdPtr->length_status |= EMAC_SWAP32(DMA_WRAP);
        pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr;
    }
    else {
        pDevCtrl->txNextBdPtr++;
    }

    /*
     * Turn on the "OWN" bit in the first buffer descriptor
     * This tells the switch that it can transmit this frame.
     */	
    firstBdPtr->length_status &= ~EMAC_SWAP32(DMA_SOP |DMA_EOP | DMA_APPEND_CRC);
    firstBdPtr->length_status |= EMAC_SWAP32(DMA_OWN | tx_flags | DMA_APPEND_CRC);
   
    /* Decrement total BD count */
    pDevCtrl->txFreeBds -= txCBPtr->nrBds;

	/*
     * Packet was enqueued correctly.
     * Advance to the next Enet_Tx_CB needing to be assigned to a BD
     */
    txCBPtr->next = NULL;
    if(pDevCtrl->txCbPtrHead == NULL) {
        pDevCtrl->txCbPtrHead = txCBPtr;
        pDevCtrl->txCbPtrTail = txCBPtr;
    }
    else{
        pDevCtrl->txCbPtrTail->next = txCBPtr;
        pDevCtrl->txCbPtrTail = txCBPtr;
    }

     /* Enable DMA for this channel */
    pDevCtrl->txDma->cfg |= DMA_ENABLE;

   /* update stats */
    pDevCtrl->stats.tx_bytes += buf_len; //((skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len);
    pDevCtrl->stats.tx_bytes += 4;
    pDevCtrl->stats.tx_packets++;

    dev->trans_start = jiffies;

    spin_unlock_irqrestore(&pDevCtrl->lock, flags);

    return 0;
}

/* --------------------------------------------------------------------------
    Name: bcmemac_xmit_multibuf
 Purpose: Send ethernet traffic in multi buffers (hdr, buf, tail)
-------------------------------------------------------------------------- */
int bcmemac_xmit_multibuf( int ch, unsigned char *hdr, int hdr_len, unsigned char *buf, int buf_len, unsigned char *tail, int tail_len, struct net_device *dev)
{
    while(bcmemac_xmit_check(dev));

    /* Header + Optional payload in two parts */
    if((hdr_len> 0) && (buf_len > 0) && (tail_len > 0) && (hdr) && (buf) && (tail)){ 
        /* Send Header */
        while(bcmemac_xmit_fragment( ch, hdr, hdr_len, DMA_SOP, dev))
            bcmemac_xmit_check(dev);
        /* Send First Fragment */  
        while(bcmemac_xmit_fragment( ch, buf, buf_len, 0, dev))
            bcmemac_xmit_check(dev);
        /* Send 2nd Fragment */ 	
        while(bcmemac_xmit_fragment( ch, tail, tail_len, DMA_EOP, dev))
            bcmemac_xmit_check(dev);
    }
    /* Header + Optional payload */
    else if((hdr_len> 0) && (buf_len > 0) && (hdr) && (buf)){
        /* Send Header */
        while(bcmemac_xmit_fragment( ch, hdr, hdr_len, DMA_SOP, dev))
            bcmemac_xmit_check(dev);
        /* Send First Fragment */
        while(bcmemac_xmit_fragment( ch, buf, buf_len, DMA_EOP, dev))
            bcmemac_xmit_check(dev);
    }
    /* Header Only (includes payload) */
    else if((hdr_len> 0) && (hdr)){ 
        /* Send Header */
        while(bcmemac_xmit_fragment( ch, hdr, hdr_len, DMA_SOP | DMA_EOP, dev))
            bcmemac_xmit_check(dev);
    }
    else{
        return 0; /* Drop the packet */
    }
    return 0;
}

/*
 * Atlanta PR5760 fix:
 *               // PR5760 - If there are no more packets available, and the last
 *               // one we saw had an overflow, we need to assume that the EMAC
 *               // has wedged and reset it.  This is the workaround for a
 *               // variant of this problem, where the MAC can stop sending us
 *               // packets altogether (the "silent wedge" condition).
 */
static int gNumberOfOverflows = 0;
static int gNoDescCount = 0;
static  int loopCount = 0;  // This should be local to the rx routine, but we need it here so that 
					    // dump_emac() can display it.  It is not really a global var.
static atomic_t resetting_EMAC = ATOMIC_INIT(0);

// PR5760 - if there are too many packets with the overflow bit set,
// then reset the EMAC.  We arrived at a threshold of 8 packets based
// on empirical analysis and testing (smaller values are too aggressive
// and larger values wait too long).
#define OVERFLOW_THRESHOLD 8
#define NODESC_THRESHOLD 20
#define RX_ETH_DATA_BUFF_SIZE       ENET_MAX_MTU_SIZE

/* These should be part of pDevCtrl, as they are not reset */
static int gNumResetsErrors = 0;
static int gNumNoDescErrors = 0;
static int gNumOverflowErrors = 0;
/* For debugging */
static unsigned int gLastErrorDmaFlag, gLastDmaFlag;

void
ResetEMAConErrors(BcmEnet_devctrl *pDevCtrl)
{
    if (1 == atomic_add_return(1, &resetting_EMAC)) {
        // Clear the overflow counter.
        gNumberOfOverflows = 0;
        gNumResetsErrors++;
        // Set the disable bit, wait for h/w to clear it, then set
        // the enable bit.
        pDevCtrl->emac->config |= EMAC_DISABLE;
        while (pDevCtrl->emac->config & EMAC_DISABLE);
            pDevCtrl->emac->config |= EMAC_ENABLE;   
    }
    // Otherwise another thread is resetting it.
    else {
        printk(KERN_NOTICE "ResetEMAConErrors: Another thread is resetting the EMAC, count=%d\n", 
        atomic_read(&resetting_EMAC));
    }
    atomic_dec(&resetting_EMAC);
}

/*
 * bcmemac_net_isr: Acknowledge interrupts and check if any packets have
 *                  arrived
 */
static irqreturn_t bcmemac_net_isr(int irq, void * dev_id, struct pt_regs * regs)
{
    BcmEnet_devctrl *pDevCtrl = dev_id;
    uint32 rxEvents;
    irqreturn_t ret = IRQ_NONE;

    /* get Rx interrupt events */
    rxEvents = pDevCtrl->rxDma->intStat;

    TRACE(("bcmemac_net_isr: intstat=%08x\n",rxEvents));

    if (rxEvents & DMA_BUFF_DONE)
    {
        pDevCtrl->rxDma->intStat = DMA_BUFF_DONE;  /* clr interrupt */
    }
    if (rxEvents & DMA_DONE)
    {
        pDevCtrl->rxDma->intStat = DMA_DONE;  // clr interrupt
// THT: For now we are not using this, will change to workqueue later on
#ifdef USE_BH       // USE_BH -- better for the over system performance
		// use bottom half (immediate queue).  Put the bottom half of interrupt in the tasklet
        queue_task(&pDevCtrl->task, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
        ret = IRQ_HANDLED;
#else
        // no BH and handle every thing inside interrupt routine
        bcmemac_rx(pDevCtrl);
        ret = IRQ_HANDLED;
#endif // USE_BH
    }
    else {
        if (rxEvents & DMA_NO_DESC) {
            pDevCtrl->rxDma->intStat = DMA_NO_DESC;

            gNumNoDescErrors++;
            printk(KERN_NOTICE "DMA_NO_DESC, count=%d, fc_alloc=%ld\n", 
                    gNoDescCount, pDevCtrl->dmaRegs->flowctl_ch1_alloc);

            pDevCtrl->rxDma->intStat = DMA_NO_DESC; // Clear the interrupt
            {
                int bdfilled;
                gNoDescCount = 0;

                bdfilled = assign_rx_buffers(pDevCtrl);
                printk(KERN_DEBUG "bcmemac_rx_isr: After assigning buffers, filled=%d\n", bdfilled);
                if (bdfilled == 0) {
                    /* For now, until we figure out why we can't reclaim the buffers */
                    printk(KERN_CRIT "Running out of buffers, all busy\n");
                }
                else {
                    /* Re-enable DMA if there are buffers available */  
                    pDevCtrl->rxDma->cfg |= DMA_ENABLE;
                }
                ResetEMAConErrors(pDevCtrl);
            }
        }
        ret =  IRQ_HANDLED; // We did handle the error condition.
    }
    return ret;
}

/*
 *  bcmemac_rx: Process all received packets.
 */
#define MAX_RX                  0x0fffffff //to disable it
static void bcmemac_rx(void *ptr)
{
    BcmEnet_devctrl *pDevCtrl = ptr;
    struct sk_buff *skb;
    unsigned long dmaFlag;
    unsigned char *pBuf;
    int len;
    int bdfilled;
    unsigned int packetLength = 0;
    int brcm_hdr_len = 0;
    int brcm_fcs_len = 0;
#ifdef VPORTS
    int vport = 0;
#if defined(CONFIG_PHY_PORT_MAPPING)
    int i;
#endif
#endif

    dmaFlag = ((EMAC_SWAP32(pDevCtrl->rxBdReadPtr->length_status)) & 0xffff);
    gLastDmaFlag = dmaFlag;
    loopCount = 0;

    /* Loop here for each received packet */
    while(!(dmaFlag & DMA_OWN) && (dmaFlag & DMA_EOP))
    {
        if (++loopCount > MAX_RX) {
            break;
        }
	   
        // Stop when we hit a buffer with no data, or a BD w/ no buffer.
        // This implies that we caught up with DMA, or that we haven't been
        // able to fill the buffers.
        if ( (pDevCtrl->rxBdReadPtr->address == (uint32) NULL)) {
            // PR5760 - If there are no more packets available, and the last
            // one we saw had an overflow, we need to assume that the EMAC
            // has wedged and reset it.  This is the workaround for a
            // variant of this problem, where the MAC can stop sending us
            // packets altogether (the "silent wedge" condition).
            if (gNumberOfOverflows > 0) {
                printk(KERN_CRIT "Handling last packet overflow, resetting pDevCtrl->emac->config, ovf=%d\n", gNumberOfOverflows);
                ResetEMAConErrors(pDevCtrl);
            }

            break;
        }

        if (dmaFlag & (EMAC_CRC_ERROR | EMAC_OV | EMAC_NO | EMAC_LG |EMAC_RXER)) {
            if (dmaFlag & EMAC_CRC_ERROR) {
                pDevCtrl->stats.rx_crc_errors++;
            }
            if (dmaFlag & EMAC_OV) {
                pDevCtrl->stats.rx_over_errors++;
            }
            if (dmaFlag & EMAC_NO) {
                pDevCtrl->stats.rx_frame_errors++;
            }
            if (dmaFlag & EMAC_LG) {
                pDevCtrl->stats.rx_length_errors++;
            }
            pDevCtrl->stats.rx_dropped++;
            pDevCtrl->stats.rx_errors++;

            /* Debug */
            gLastErrorDmaFlag = dmaFlag;

            /* THT Starting with 7110B0 (Atlanta's PR5760), look for resetting the 
             * EMAC on overflow condition
             */
            if ((dmaFlag & DMA_OWN) == 0) {
                uint32 bufferAddress;

                // PR5760 - keep track of the number of overflow packets.
                if (dmaFlag & EMAC_OV) {
                    gNumberOfOverflows++;
                    gNumOverflowErrors++;
                }

                // Keep a running total of the packet length.
                packetLength += ((EMAC_SWAP32(pDevCtrl->rxBdReadPtr->length_status))>>16);

                // Pull the buffer from the BD and clear the BD so that it can be
                // reassigned later.
                bufferAddress = (uint32) EMAC_SWAP32(pDevCtrl->rxBdReadPtr->address);
                pDevCtrl->rxBdReadPtr->address = 0;

                pBuf = (unsigned char *)KSEG0ADDR(bufferAddress);
                skb = (struct sk_buff *)*(unsigned long *)(pBuf-4);
                atomic_set(&skb->users, 1);

                if (atomic_read(&pDevCtrl->rxDmaRefill) == 0) {
                    bdfilled = assign_rx_buffers(pDevCtrl);
                }

                /* Advance BD ptr to next in ring */
                IncRxBDptr(pDevCtrl->rxBdReadPtr, pDevCtrl);

                // If this is the last buffer in the packet, stop.
                if (dmaFlag & DMA_EOP) {
                    packetLength = 0;
                }
            }

            // Clear the EMAC block receive logic for oversized packets.  On
            // a really, really long packet (often caused by duplex
            // mismatches), the EMAC_LG bit may not be set, so I need to
            // check for this condition separately.
            if ((packetLength >= 2048) ||

            // PR5760 - if there are too many packets with the overflow bit set,
            // then reset the EMAC.  We arrived at a threshold of 8 packets based
            // on empirical analysis and testing (smaller values are too aggressive
            // and larger values wait too long).
                (gNumberOfOverflows > OVERFLOW_THRESHOLD)) {
                ResetEMAConErrors(pDevCtrl);
            }

            //Read more until EOP or good packet
            dmaFlag = ((EMAC_SWAP32(pDevCtrl->rxBdReadPtr->length_status)) & 0xffff);
            gLastDmaFlag = dmaFlag; 

            /* Exit if we surpass number of packets */
            continue;
        }/* if error packet */

        gNumberOfOverflows = 0;
        gNoDescCount = 0;

        pBuf = (unsigned char *)(KSEG0ADDR(EMAC_SWAP32(pDevCtrl->rxBdReadPtr->address)));

        /*
         * THT: Invalidate the RAC cache again, since someone may have read near the vicinity
         * of the buffer.  This is necessary because the RAC cache is much larger than the CPU cache
         */
        bcm_inv_rac_all();

        len = ((EMAC_SWAP32(pDevCtrl->rxBdReadPtr->length_status))>>16);
        /* Null the BD field to prevent reuse */
        pDevCtrl->rxBdReadPtr->length_status &= EMAC_SWAP32(0xffff0000); //clear status.
        pDevCtrl->rxBdReadPtr->address = EMAC_SWAP32(0);

        /* Advance BD ptr to next in ring */
        IncRxBDptr(pDevCtrl->rxBdReadPtr, pDevCtrl);
        // Recover the SKB pointer saved during assignment.
        skb = (struct sk_buff *)*(unsigned long *)(pBuf-4);

#ifdef VPORTS
        vport = 0;
        if (is_vport(pDevCtrl->dev)&& is_vmode()) {
            unsigned char *p;

            vport = ingress_vport_id_from_tag(pBuf) + 1;
#if defined(CONFIG_PHY_PORT_MAPPING)
            for (i = 1; i <= sizeof(pDevCtrl->phy_port)/sizeof(int); i++) 
            {
                if ((vnet_dev[i] != NULL) && (vnet_dev[i]->base_addr >> PHY_PORT == vport))
                {
                    vport = i;
                    break;
                }
            }
#else
            vport = (vnet_dev[vport] == NULL) ? 0: vport;
#endif
            for (p = pBuf + 11; p >= pBuf; p--)
                *(p + 6) = *p;

            brcm_hdr_len = 6;
            brcm_fcs_len = 4;
        }
#endif
        dmaFlag = ((EMAC_SWAP32(pDevCtrl->rxBdReadPtr->length_status))&0xffff);
        gLastDmaFlag = dmaFlag;

        skb_pull(skb, brcm_hdr_len);
        skb_trim(skb, len - ETH_CRC_LEN - brcm_hdr_len - brcm_fcs_len);

#ifdef DUMP_DATA
        printk("bcmemac_rx :");
        dumpHexData(skb->data, 32);
#endif

        /* Finish setting up the received SKB and send it to the kernel */
#ifdef VPORTS
        if ((vport > 0) && (vport <= pDevCtrl->EnetInfo.numSwitchPorts)) {
            struct net_device_stats *s = 
                (struct net_device_stats *)netdev_priv(vnet_dev[vport]);

            skb->dev = vnet_dev[vport];
            skb->protocol = eth_type_trans(skb, vnet_dev[vport]);
            s->rx_packets ++;
            s->rx_bytes += len;
        }
        else
#endif
        {
            skb->dev = pDevCtrl->dev;
            skb->protocol = eth_type_trans(skb, pDevCtrl->dev);
        }

        /* Allocate a new SKB for the ring */
        if (atomic_read(&pDevCtrl->rxDmaRefill) == 0) {
            bdfilled = assign_rx_buffers(pDevCtrl);
        }

        pDevCtrl->stats.rx_packets++;
        pDevCtrl->stats.rx_bytes += len;

        /* Notify kernel */
        netif_rx(skb);

        pDevCtrl->dev->last_rx = jiffies;

        continue;
    }
    loopCount = -1; // tell dump_emac that we are outside RX
}


/*
 * Set the hardware MAC address.
 */
static int bcm_set_mac_addr(struct net_device *dev, void *p)
{
    struct sockaddr *addr=p;

    if(netif_running(dev))
        return -EBUSY;

    memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

    write_mac_address(dev);

    return 0;
}

/*
 * write_mac_address: store MAC address into EMAC perfect match registers                   
 */
void write_mac_address(struct net_device *dev)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)dev->priv;
    volatile uint32 data32bit;
#ifdef VPORTS
    if (is_vport(dev))
        pDevCtrl = netdev_priv(vnet_dev[0]);
#endif

    ASSERT(pDevCtrl != NULL);

    data32bit = pDevCtrl->emac->config;
    if (data32bit & EMAC_ENABLE) {
        /* disable ethernet MAC while updating its registers */
        pDevCtrl->emac->config &= ~EMAC_ENABLE ;           
        pDevCtrl->emac->config |= EMAC_DISABLE ;           
        while(pDevCtrl->emac->config & EMAC_DISABLE);     
    }

    /* add our MAC address to the perfect match register */
    perfectmatch_update((BcmEnet_devctrl *)dev->priv, dev->dev_addr, 1);

    if (data32bit & EMAC_ENABLE) {
        pDevCtrl->emac->config = data32bit;
    }
}


/*******************************************************************************
*
* skb_headerinit
*
* Reinitializes the socket buffer.  Lifted from skbuff.c
*
* RETURNS: None.
*/

static inline void skb_headerinit(void *p, kmem_cache_t *cache, 
        unsigned long flags)
{
    struct sk_buff *skb = p;

    skb->next = NULL;
    skb->prev = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
    skb->list = NULL;
    skb->stamp.tv_sec=0;    /* No idea about time */
    skb->security = 0;  /* By default packets are insecure */
#endif
    skb->sk = NULL;
    skb->dev = NULL;
    skb->dst = NULL;
    /* memset(skb->cb, 0, sizeof(skb->cb)); */
    skb->pkt_type = PACKET_HOST;    /* Default type */
    skb->ip_summed = 0;
    skb->priority = 0;
    skb->destructor = NULL;

#ifdef CONFIG_NETFILTER
    skb->nfmark = /*skb->nfcache =*/ 0;
    skb->nfct = NULL;
#ifdef CONFIG_NETFILTER_DEBUG
    skb->nf_debug = 0;
#endif
#endif
#ifdef CONFIG_NET_SCHED
    skb->tc_index = 0;
#endif
}

static void handleAlignment(BcmEnet_devctrl *pDevCtrl, struct sk_buff* skb)
{
    /* Even that the EMAC now accept any buffers aligned on a 4-B boundary, we still gain
     * better performance if we align the buffer on a 256-B boundary
     */
    // We will reserve 20 bytes, and wants that skb->data to be on 
    // a 256B boundary.

    volatile unsigned long boundary256, curData, resHeader;

    curData = (unsigned long) skb->data;
    boundary256 = (curData+20+255) & 0xffffff00;
    resHeader = boundary256 - curData;

    // If we don't have room for 20B, go to next boundary.  Sanity check
    if (resHeader < 20) {
        boundary256 += 256;
    }
    resHeader = boundary256 - curData - 4;
    // This advances skb->data 4B shy of the boundary,
    // and also includes the 16B headroom commented out below,
    // by virtue of our sanity check above.
    skb_reserve(skb, resHeader);

    /* reserve space ditto __dev_alloc_skb */
    // skb_reserve(skb, 16);

    *(unsigned int *)skb->data = (unsigned int)skb;
    skb_reserve(skb, 4);

    // Make sure it is on 256B boundary, should never happen if our
    // calculation was correct.
    if ((unsigned long) skb->data & 0xff) {
        printk("$$$$$$$$$$$$$$$$$$$ skb buffer is NOT aligned on 256B boundary\n");
    }

    if (pDevCtrl->bIPHdrOptimize) {
        /* Advance the data pointer 2 bytes */
        skb_reserve(skb, 2);
    }
}

/*
 * assign_rx_buffers: Beginning with the first receive buffer descriptor
 *                  used to receive a packet since this function last
 *                  assigned a buffer, reassign buffers to receive
 *                  buffer descriptors, and mark them as EMPTY; available
 *                  to be used again.
 *
 */
static int assign_rx_buffers(BcmEnet_devctrl *pDevCtrl)
{
    struct sk_buff *skb;
    uint16  bdsfilled=0;
    int devInUsed;
    int i;

    /*
     * check assign_rx_buffers is in process?
     * This function may be called from various context, like timer
     * or bcmemac_rx
     */
    if(test_and_set_bit(0, &pDevCtrl->rxbuf_assign_busy)) {
        return bdsfilled;
    }

    /* loop here for each buffer needing assign */
    for (;;)
    {
        /* exit if no receive buffer descriptors are in "unused" state */
        if(EMAC_SWAP32(pDevCtrl->rxBdAssignPtr->address) != 0)
        {
            break;
        }

        skb = pDevCtrl->skb_pool[pDevCtrl->nextskb++];
        if (pDevCtrl->nextskb == MAX_RX_BUFS)
            pDevCtrl->nextskb = 0;

        /* check if skb is free */
        if (skb_shared(skb) || 
            atomic_read(skb_dataref(skb)) > 1) {
            /* find next free skb */
            for (i = 0; i < MAX_RX_BUFS; i++) {
                skb = pDevCtrl->skb_pool[pDevCtrl->nextskb++];
                if (pDevCtrl->nextskb == MAX_RX_BUFS)
                    pDevCtrl->nextskb = 0;
                if ((skb_shared(skb) == 0) && 
                    atomic_read(skb_dataref(skb)) <= 1) {
                    /* found a free skb */
                    break;
                }
            }
            if (i == MAX_RX_BUFS) {
                /* no free skb available now */
                /* rxDma is empty, set flag and let timer function to refill later */
                atomic_set(&pDevCtrl->rxDmaRefill, 1);
                break;
            }
        }

        atomic_set(&pDevCtrl->rxDmaRefill, 0);
        skb_headerinit(skb, NULL, 0);  /* clean state */

        /* init the skb, in case other app. modified the skb pointer */
        skb->data = skb->tail = skb->head;
        skb->end = skb->data + (skb->truesize - sizeof(struct sk_buff));
        skb->len = 0;
        skb->data_len = 0;
        skb->cloned = 0;

        handleAlignment(pDevCtrl, skb);

        skb_put(skb, pDevCtrl->rxBufLen);

        /*
         * Set the user count to two so when the upper layer frees the
         * socket buffer, only the user count is decremented.
         */
        atomic_inc(&skb->users);

        /* kept count of any BD's we refill */
        bdsfilled++;

        dma_cache_wback_inv((unsigned long)skb->data, pDevCtrl->rxBufLen);


        /* assign packet, prepare descriptor, and advance pointer */
        if (pDevCtrl->bIPHdrOptimize) {
            unsigned long dmaAddr = (unsigned long) skb->data-2;
            if (dmaAddr & 0xff) {
                printk("@@@@@@@@@@@@@@@ DMA Address %08x not aligned\n", (unsigned int) dmaAddr);
            }
            pDevCtrl->rxBdAssignPtr->address = EMAC_SWAP32((uint32)CPHYSADDR(dmaAddr));
        }
        else {
            pDevCtrl->rxBdAssignPtr->address = EMAC_SWAP32((uint32)CPHYSADDR(skb->data));
        }
        pDevCtrl->rxBdAssignPtr->length_status  = EMAC_SWAP32((pDevCtrl->rxBufLen<<16));

        IncFlowCtrlAllocRegister(pDevCtrl);

        /* turn on the newly assigned BD for DMA to use */
        if (pDevCtrl->rxBdAssignPtr == pDevCtrl->rxLastBdPtr) {
            pDevCtrl->rxBdAssignPtr->length_status |= EMAC_SWAP32(DMA_OWN | DMA_WRAP);
            pDevCtrl->rxBdAssignPtr = pDevCtrl->rxFirstBdPtr;
        }
        else {
            pDevCtrl->rxBdAssignPtr->length_status |= EMAC_SWAP32(DMA_OWN);
            pDevCtrl->rxBdAssignPtr++;
        }
    }

    /*
     * restart DMA in case the DMA ran out of descriptors
     */
    devInUsed = atomic_read(&pDevCtrl->devInUsed);
    if (devInUsed > 0) {
        pDevCtrl->rxDma->cfg |= DMA_ENABLE;
    }

    clear_bit(0, &pDevCtrl->rxbuf_assign_busy);

    return bdsfilled;
}

/*
 * perfectmatch_write: write an address to the EMAC perfect match registers
 */
static void perfectmatch_write(volatile EmacRegisters *emac, int reg, const uint8 * pAddr, bool bValid)
{
    volatile uint32 *pmDataLo;
    volatile uint32 *pmDataHi;
  
    switch (reg) {
    case 0:
        pmDataLo = &emac->pm0DataLo;
        pmDataHi = &emac->pm0DataHi;
        break;
    case 1:
        pmDataLo = &emac->pm1DataLo;
        pmDataHi = &emac->pm1DataHi;
        break;
    case 2:
        pmDataLo = &emac->pm2DataLo;
        pmDataHi = &emac->pm2DataHi;  		/* PR 10861 - fixed wrong value here */
        break;
    case 3:
        pmDataLo = &emac->pm3DataLo;
        pmDataHi = &emac->pm3DataHi;
        break;
    case 4:
        pmDataLo = &emac->pm4DataLo;
        pmDataHi = &emac->pm4DataHi;
        break;
    case 5:
        pmDataLo = &emac->pm5DataLo;
        pmDataHi = &emac->pm5DataHi;
        break;
    case 6:
        pmDataLo = &emac->pm6DataLo;
        pmDataHi = &emac->pm6DataHi;  		/* PR 10861 - fixed wrong value here */
        break;
    case 7:
        pmDataLo = &emac->pm7DataLo;
        pmDataHi = &emac->pm7DataHi;
        break;		
    default:
        return;
    }
    /* Fill DataHi/Lo */
    if (bValid == 1)
        *pmDataLo = MAKE4((pAddr + 2));
    *pmDataHi = MAKE2(pAddr) | (bValid << 16);
}

/*
 * perfectmatch_update: update an address to the EMAC perfect match registers
 */
static void perfectmatch_update(BcmEnet_devctrl *pDevCtrl, const uint8 * pAddr, bool bValid)
{
    int i;
    unsigned int aged_ref;
    int aged_entry;

    /* check if this address is in used */
    for (i = 0; i < MAX_PMADDR; i++) {
        if (pDevCtrl->pmAddr[i].valid == 1) {
            if (memcmp (pDevCtrl->pmAddr[i].dAddr, pAddr, ETH_ALEN) == 0) {
                if (bValid == 0) {
                    /* clear the valid bit in PM register */
                    perfectmatch_write(pDevCtrl->emac, i, pAddr, bValid);
                    /* clear the valid bit in pDevCtrl->pmAddr */
                    pDevCtrl->pmAddr[i].valid = bValid;
                } else {
                    pDevCtrl->pmAddr[i].ref++;
                }
                return;
            }
        }
    }
    if (bValid == 1) {
        for (i = 0; i < MAX_PMADDR; i++) {
            /* find an available entry for write pm address */
            if (pDevCtrl->pmAddr[i].valid == 0) {
                break;
            }
        }
        if (i == MAX_PMADDR) {
            aged_ref = 0xFFFFFFFF;
            aged_entry = 0;
            /* aged out an entry */
            for (i = 0; i < MAX_PMADDR; i++) {
                if (pDevCtrl->pmAddr[i].ref < aged_ref) {
                    aged_ref = pDevCtrl->pmAddr[i].ref;
                    aged_entry = i;
                }
            }
            i = aged_entry;
        }
        /* find a empty entry and add the address */
        perfectmatch_write(pDevCtrl->emac, i, pAddr, bValid);

        /* update the pDevCtrl->pmAddr */
        pDevCtrl->pmAddr[i].valid = bValid;
        memcpy(pDevCtrl->pmAddr[i].dAddr, pAddr, ETH_ALEN);
        pDevCtrl->pmAddr[i].ref = 1;
    }
}

#ifdef MULTICAST_HW_FILTER
/*
 * perfectmatch_clean: Clear EMAC perfect match registers not matched by pAddr (PR10861)
 */
static void perfectmatch_clean(BcmEnet_devctrl *pDevCtrl, const uint8 * pAddr)
{
    int i;

    /* check if this address is in used */
    for (i = 0; i < MAX_PMADDR; i++) {
        if (pDevCtrl->pmAddr[i].valid == 1) {
            if (memcmp (pDevCtrl->pmAddr[i].dAddr, pAddr, ETH_ALEN) != 0) {
                /* clear the valid bit in PM register */
                perfectmatch_write(pDevCtrl->emac, i, pAddr, 0);
                /* clear the valid bit in pDevCtrl->pmAddr */
                pDevCtrl->pmAddr[i].valid = 0;
            }
        }
    }
}
#endif

/*
 * clear_mib: Initialize the Ethernet Switch MIB registers
 */
static void clear_mib(volatile EmacRegisters *emac)
{
    int                   i;
    volatile uint32       *pt;

    pt = (uint32 *)&emac->tx_mib;
    for( i = 0; i < NumEmacTxMibVars; i++ ) {
        *pt++ = 0;
    }

    pt = (uint32 *)&emac->rx_mib;;
    for( i = 0; i < NumEmacRxMibVars; i++ ) {
        *pt++ = 0;
    }
}

/*
 * init_emac: Initializes the Ethernet Switch control registers
 */
static int init_emac(BcmEnet_devctrl *pDevCtrl)
{
    volatile EmacRegisters *emac;

    TRACE(("bcmemacenet: init_emac\n"));

    pDevCtrl->emac = (volatile EmacRegisters * const)ENET_MAC_ADR_BASE;
    emac = pDevCtrl->emac;

    if (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH)
    {
        init_ethsw_pinmux(pDevCtrl);
        get_phy_port_config(pDevCtrl->phy_port, sizeof(pDevCtrl->phy_port)/sizeof(int));
    }

    /* Initialize the Ethernet Switch MIB registers */
    clear_mib(pDevCtrl->emac);

    /* disable ethernet MAC while updating its registers */
    emac->config = EMAC_DISABLE ;
    while(emac->config & EMAC_DISABLE);

    /* issue soft reset, wait for it to complete */
    emac->config = EMAC_SOFT_RESET;
    while (emac->config & EMAC_SOFT_RESET);

    if (mii_init(pDevCtrl->dev))
        return -EFAULT;

    /* Initialize emac registers */
    /* Ethernet header optimization, reserve 2 bytes at head of packet */
    if (pDevCtrl->bIPHdrOptimize) {
        unsigned int packetOffset = (2 << RX_CONFIG_PKT_OFFSET_SHIFT);
        emac->rxControl = EMAC_FC_EN | EMAC_UNIFLOW | packetOffset;
    }
    else {
        emac->rxControl = EMAC_FC_EN | EMAC_UNIFLOW;  // THT dropped for RR support | EMAC_PROM;   // allow Promiscuous
    }

#ifdef MAC_LOOPBACK
    emac->rxControl |= EMAC_LOOPBACK;
#endif
    emac->rxMaxLength = ENET_MAX_MTU_SIZE+(pDevCtrl->bIPHdrOptimize ? 2: 0);
    emac->txMaxLength = ENET_MAX_MTU_SIZE;

    /* tx threshold = abs(63-(0.63*EMAC_DMA_MAX_BURST_LENGTH)) */
    emac->txThreshold = EMAC_TX_WATERMARK;  // THT PR12238 as per David Ferguson: Turning off RR, Was 32
    emac->mibControl  = EMAC_NO_CLEAR;
    emac->intMask = 0;              /* mask all EMAC interrupts*/

    TRACE(("done init emac\n"));

    return 0;

}

/*
 * init_dma: Initialize DMA control register
 */
static void init_IUdma(BcmEnet_devctrl *pDevCtrl)
{
    TRACE(("bcmemacenet: init_dma\n"));

    /*
     * initialize IUDMA controller register
     */
    //pDevCtrl->dmaRegs->controller_cfg = DMA_FLOWC_CH1_EN;
    pDevCtrl->dmaRegs->controller_cfg = 0;
    pDevCtrl->dmaRegs->flowctl_ch1_thresh_lo = DMA_FC_THRESH_LO;
    pDevCtrl->dmaRegs->flowctl_ch1_thresh_hi = DMA_FC_THRESH_HI;

    // transmit
    pDevCtrl->txDma->cfg = 0;       /* initialize first (will enable later) */
    pDevCtrl->txDma->maxBurst = DMA_MAX_BURST_LENGTH; /*DMA_MAX_BURST_LENGTH;*/

    pDevCtrl->txDma->intMask = 0;   /* mask all ints */
    /* clr any pending interrupts on channel */
    pDevCtrl->txDma->intStat = DMA_DONE|DMA_NO_DESC|DMA_BUFF_DONE;
    /* set to interrupt on packet complete */
    pDevCtrl->txDma->intMask = 0;

	pDevCtrl->txDma->descPtr = (uint32)CPHYSADDR(pDevCtrl->txFirstBdPtr);

    // receive
    pDevCtrl->rxDma->cfg = 0;  // initialize first (will enable later)
    pDevCtrl->rxDma->maxBurst = DMA_MAX_BURST_LENGTH; /*DMA_MAX_BURST_LENGTH;*/

    pDevCtrl->rxDma->descPtr = (uint32)CPHYSADDR(pDevCtrl->rxFirstBdPtr);
    pDevCtrl->rxDma->intMask = 0;   /* mask all ints */
    /* clr any pending interrupts on channel */
    pDevCtrl->rxDma->intStat = DMA_DONE|DMA_NO_DESC|DMA_BUFF_DONE;
    /* set to interrupt on packet complete and no descriptor available */
    pDevCtrl->rxDma->intMask = DMA_DONE; //|DMA_NO_DESC;
}

/*
 *  init_buffers: initialize driver's pools of receive buffers
 *  and tranmit headers
 */
static int init_buffers(BcmEnet_devctrl *pDevCtrl)
{
    struct sk_buff *skb;
    int bdfilled;
    int i;

    /* set initial state of all BD pointers to top of BD ring */
    pDevCtrl->txCbPtrHead = pDevCtrl->txCbPtrTail = NULL;

    /* allocate recieve buffer pool */
    for (i = 0; i < MAX_RX_BUFS; i++) {
        /* allocate a new SKB for the ring */
        /* 4 bytes for skb pointer, 256B for alignment, plus 2 bytes for IP header optimization */
        skb = dev_alloc_skb(pDevCtrl->rxBufLen + 4 + 256);
        if (skb == NULL)
        {
            printk(KERN_NOTICE CARDNAME": Low memory.\n");
            break;
        }
        /* save skb pointer */
        pDevCtrl->skb_pool[i] = skb;
    }

    if (i < MAX_RX_BUFS) {
        /* release allocated receive buffer memory */
        for (i = 0; i < MAX_RX_BUFS; i++) {
            if (pDevCtrl->skb_pool[i] != NULL) {
                dev_kfree_skb (pDevCtrl->skb_pool[i]);
                pDevCtrl->skb_pool[i] = NULL;
            }
        }
        return -ENOMEM;
    }
    /* init the next free skb index */
    pDevCtrl->nextskb = 0;
    atomic_set(&pDevCtrl->rxDmaRefill, 0);
    clear_bit(0, &pDevCtrl->rxbuf_assign_busy);

    /* assign packet buffers to all available Dma descriptors */
    bdfilled = assign_rx_buffers(pDevCtrl);
    if (bdfilled > 0) {
        printk("init_buffers: %d descriptors initialized\n", bdfilled);
    }
    // This avoid depending on flowclt_alloc which may go negative during init
    pDevCtrl->dmaRegs->flowctl_ch1_alloc = IUDMA_CH1_FLOW_ALLOC_FORCE | bdfilled;
    printk("init_buffers: %08lx descriptors initialized, from flowctl\n", pDevCtrl->dmaRegs->flowctl_ch1_alloc);

    return 0;
}

/*
 * bcmemac_init_dev: initialize Ethernet Switch device,
 * allocate Tx/Rx buffer descriptors pool, Tx control block pool.
 */
static int bcmemac_init_dev(BcmEnet_devctrl *pDevCtrl)
{
    int i;
    int nrCbs;
    void *p;
    Enet_Tx_CB *txCbPtr;

    pDevCtrl->rxIrq = BCM_LINUX_CPU_ENET_IRQ;
    /* setup buffer/pointer relationships here */
    pDevCtrl->nrTxBds = NR_TX_BDS;
    pDevCtrl->nrRxBds = NR_RX_BDS;
    pDevCtrl->rxBufLen = ENET_MAX_MTU_SIZE + (pDevCtrl->bIPHdrOptimize ? 2: 0);

    /* init rx/tx dma channels */
    pDevCtrl->dmaRegs = (DmaRegs *)(DMA_ADR_BASE);
    pDevCtrl->rxDma = &pDevCtrl->dmaRegs->chcfg[EMAC_RX_CHAN];
    pDevCtrl->txDma = &pDevCtrl->dmaRegs->chcfg[EMAC_TX_CHAN];
    pDevCtrl->rxBds = (DmaDesc *) EMAC_RX_DESC_BASE;
    pDevCtrl->txBds = (DmaDesc *) EMAC_TX_DESC_BASE;
	
    /* alloc space for the tx control block pool */
    nrCbs = pDevCtrl->nrTxBds; 
    if (!(p = kmalloc(nrCbs*sizeof(Enet_Tx_CB), GFP_KERNEL))) {
        return -ENOMEM;
    }
    memset(p, 0, nrCbs*sizeof(Enet_Tx_CB));
    pDevCtrl->txCbs = (Enet_Tx_CB *)p;/* tx control block pool */

    /* initialize rx ring pointer variables. */
    pDevCtrl->rxBdAssignPtr = pDevCtrl->rxBdReadPtr =
                pDevCtrl->rxFirstBdPtr = pDevCtrl->rxBds;
    pDevCtrl->rxLastBdPtr = pDevCtrl->rxFirstBdPtr + pDevCtrl->nrRxBds - 1;

    /* init the receive buffer descriptor ring */
    for (i = 0; i < pDevCtrl->nrRxBds; i++)
    {
        (pDevCtrl->rxFirstBdPtr + i)->length_status = EMAC_SWAP32((pDevCtrl->rxBufLen<<16));
        (pDevCtrl->rxFirstBdPtr + i)->address = EMAC_SWAP32(0);
    }
    pDevCtrl->rxLastBdPtr->length_status |= EMAC_SWAP32(DMA_WRAP);

    /* init transmit buffer descriptor variables */
    pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr = pDevCtrl->txBds;
    pDevCtrl->txLastBdPtr = pDevCtrl->txFirstBdPtr + pDevCtrl->nrTxBds - 1;

    /* clear the transmit buffer descriptors */
    for (i = 0; i < pDevCtrl->nrTxBds; i++)
    {
        (pDevCtrl->txFirstBdPtr + i)->length_status = EMAC_SWAP32((0<<16));
        (pDevCtrl->txFirstBdPtr + i)->address = EMAC_SWAP32(0);
    }
    pDevCtrl->txLastBdPtr->length_status |= EMAC_SWAP32(DMA_WRAP);
    pDevCtrl->txFreeBds = pDevCtrl->nrTxBds;

    /* initialize the receive buffers and transmit headers */
    if (init_buffers(pDevCtrl)) {
        kfree((void *)pDevCtrl->txCbs);
        return -ENOMEM;
    }

    for (i = 0; i < nrCbs; i++)
    {
        txCbPtr = pDevCtrl->txCbs + i;
        txCb_enq(pDevCtrl, txCbPtr);
    }

    /* init dma registers */
    init_IUdma(pDevCtrl);

    /* init switch control registers */
    if (init_emac(pDevCtrl)) {
        kfree((void *)pDevCtrl->txCbs);
        return -EFAULT;
    }

#ifdef IUDMA_INIT_WORKAROUND
    {
        unsigned long diag_rdbk;

        pDevCtrl->dmaRegs->enet_iudma_diag_ctl = 0x100; /* enable to read diags. */
        diag_rdbk = pDevCtrl->dmaRegs->enet_iudma_diag_rdbk;

        pDevCtrl->rxBdAssignPtr = pDevCtrl->rxBdReadPtr = pDevCtrl->rxFirstBdPtr + ((diag_rdbk>>1)&DESC_MASK);
        pDevCtrl->txNextBdPtr = pDevCtrl->txFirstBdPtr + ((diag_rdbk>>(16+1))&DESC_MASK);
    }
#endif	

    /* if we reach this point, we've init'ed successfully */
    return 0;
}

#ifdef USE_PROC

#define PROC_DUMP_DMADESC_STATUS    0
#define PROC_DUMP_EMAC_REGISTERS    1

#define PROC_DUMP_TXBD_1of6         2
#define PROC_DUMP_TXBD_2of6         3
#define PROC_DUMP_TXBD_3of6         4
#define PROC_DUMP_TXBD_4of6         5
#define PROC_DUMP_TXBD_5of6         6
#define PROC_DUMP_TXBD_6of6         7

#define PROC_DUMP_RXBD_1of6         8
#define PROC_DUMP_RXBD_2of6         9
#define PROC_DUMP_RXBD_3of6         10
#define PROC_DUMP_RXBD_4of6         11
#define PROC_DUMP_RXBD_5of6         12
#define PROC_DUMP_RXBD_6of6         13

#define PROC_DUMP_SKB_1of6          14
#define PROC_DUMP_SKB_2of6          15
#define PROC_DUMP_SKB_3of6          16
#define PROC_DUMP_SKB_4of6          17
#define PROC_DUMP_SKB_5of6          18
#define PROC_DUMP_SKB_6of6          19


/*
 * bcmemac_net_dump - display EMAC information
 */
int bcmemac_net_dump(BcmEnet_devctrl *pDevCtrl, char *buf, int reqinfo)
{
    int len = 0;
#if 0
    int  i;
    struct sk_buff *skb;
    static int bufcnt;
#endif

    switch (reqinfo) {

#if 0

/*----------------------------- TXBD --------------------------------*/
    case PROC_DUMP_TXBD_1of6: /* tx DMA BD descriptor ring 1 of 6 */
        len += sprintf(&buf[len], "\ntx buffer descriptor ring status.\n");
        len += sprintf(&buf[len], "BD\tlocation\tlength\tstatus addr\n");
//	 len += sprintf(&buf[len], "First 1/6 of %d buffers\n", pDevCtrl->nrTxBds);
        for (i = 0; i < pDevCtrl->nrTxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

    case PROC_DUMP_TXBD_2of6: /* tx DMA BD descriptor ring, 2 of 6 */
        for (i = pDevCtrl->nrTxBds/6; i < pDevCtrl->nrTxBds/3; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

   case PROC_DUMP_TXBD_3of6: /* tx DMA BD descriptor ring, 3 of 6 */
        for (i = pDevCtrl->nrTxBds/3; i < pDevCtrl->nrTxBds/2; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

   case PROC_DUMP_TXBD_4of6: /* tx DMA BD descriptor ring, 4 of 6 */
        for (i = pDevCtrl->nrTxBds/2; i < 2*pDevCtrl->nrTxBds/3; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

  case PROC_DUMP_TXBD_5of6: /* tx DMA BD descriptor ring, 5 of 6 */
        for (i = 2*pDevCtrl->nrTxBds/3; i < 5*pDevCtrl->nrTxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

 case PROC_DUMP_TXBD_6of6: /* tx DMA BD descriptor ring, 5 of 6 */
        for (i = 5*pDevCtrl->nrTxBds/6; i < pDevCtrl->nrTxBds; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->txBds[i],
                   (pDevCtrl->txBds[i].length_status>>16),
                   (pDevCtrl->txBds[i].length_status&0xffff),
                   (pDevCtrl->txBds[i].address));
        }
        break;

/*----------------------------- RXBD --------------------------------*/
    case PROC_DUMP_RXBD_1of6: /* rx DMA BD descriptor ring, 1 of 6 */
        len += sprintf(&buf[len], "\nrx buffer descriptor ring status.\n");
        len += sprintf(&buf[len], "BD\tlocation\tlength\tstatus\n");
        for (i = 0; i < pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(unsigned int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
        break;

   case PROC_DUMP_RXBD_2of6: /* rx DMA BD descriptor ring, 2 of 6 */
        for (i = pDevCtrl->nrRxBds/6; i < 2*pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
		break;

   case PROC_DUMP_RXBD_3of6: /* rx DMA BD descriptor ring, 3 of 6 */
        for (i = 2*pDevCtrl->nrRxBds/6; i < 3*pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
		break;

  case PROC_DUMP_RXBD_4of6: /* rx DMA BD descriptor ring, 4 of 6 */
        for (i = 3*pDevCtrl->nrRxBds/6; i < 4*pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
		break;

  case PROC_DUMP_RXBD_5of6: /* rx DMA BD descriptor ring, 5 of 6 */
        for (i = 4*pDevCtrl->nrRxBds/6; i < 5*pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
		break;

  case PROC_DUMP_RXBD_6of6: /* rx DMA BD descriptor ring, 5 of 6 */
        for (i = 5*pDevCtrl->nrRxBds/6; i < 6*pDevCtrl->nrRxBds/6; i++)
        {
            len += sprintf(&buf[len], "%03d\t%08x\t%04d\t%04x %08lx\n",
                   i,(int)&pDevCtrl->rxBds[i],
                   (pDevCtrl->rxBds[i].length_status>>16),
                   (pDevCtrl->rxBds[i].length_status&0xffff),
				   (pDevCtrl->rxBds[i].address));
        }
        break;
#endif

    case PROC_DUMP_DMADESC_STATUS:  /* DMA descriptors pointer and status */
        len += sprintf(&buf[len], "\nrx pointers:\n");
        len += sprintf(&buf[len], "DmaDesc *rxBds:\t\t%08x\n",(unsigned int)pDevCtrl->rxBds);
        len += sprintf(&buf[len], "DmaDesc *rxBdAssignPtr:\t%08x\n",(unsigned int)pDevCtrl->rxBdAssignPtr);
        len += sprintf(&buf[len], "DmaDesc *rxBdReadPtr:\t%08x\n",(unsigned int)pDevCtrl->rxBdReadPtr);
        len += sprintf(&buf[len], "DmaDesc *rxLastBdPtr:\t%08x\n",(unsigned int)pDevCtrl->rxLastBdPtr);
        len += sprintf(&buf[len], "DmaDesc *rxFirstBdPtr:\t%08x\n",(unsigned int)pDevCtrl->rxFirstBdPtr);

        len += sprintf(&buf[len], "\ntx pointers:\n");
        len += sprintf(&buf[len], "DmaDesc *txBds:\t\t%08x\n",(unsigned int)pDevCtrl->txBds);
        len += sprintf(&buf[len], "DmaDesc *txLastBdPtr:\t%08x\n",(unsigned int)pDevCtrl->txLastBdPtr);
        len += sprintf(&buf[len], "DmaDesc *txFirstBdPtr:\t%08x\n",(unsigned int)pDevCtrl->txFirstBdPtr);
        len += sprintf(&buf[len], "DmaDesc *txNextBdPtr:\t%08x\n",(unsigned int)pDevCtrl->txNextBdPtr);
        len += sprintf(&buf[len], "Enet_Tx_CB *txCbPtrHead:%08x\n",(unsigned int)pDevCtrl->txCbPtrHead);
        if (pDevCtrl->txCbPtrHead)
            len += sprintf(&buf[len], "txCbPtrHead->lastBdAddr:\t%08x\n",(unsigned int)pDevCtrl->txCbPtrHead->lastBdAddr);
        len += sprintf(&buf[len], "EnetTxCB *txCbPtrTail:\t%08x\n",(unsigned int)pDevCtrl->txCbPtrTail);
        if (pDevCtrl->txCbPtrTail)
            len += sprintf(&buf[len], "txCbPtrTail->lastBdAddr:\t%08x\n",(unsigned int)pDevCtrl->txCbPtrTail->lastBdAddr);
        len += sprintf(&buf[len], "txFreeBds (TxBDs %d):\t%d\n", NR_TX_BDS, (unsigned int) pDevCtrl->txFreeBds);

        len += sprintf(&buf[len], "\ntx DMA Channel Config\t\t%08x\n", (unsigned int) pDevCtrl->txDma->cfg);
        len += sprintf(&buf[len], "tx DMA Intr Control/Status\t%08x\n", (unsigned int) pDevCtrl->txDma->intStat);
        len += sprintf(&buf[len], "tx DMA Intr Mask\t\t%08x\n", (unsigned int) pDevCtrl->txDma->intMask);
        //len += sprintf(&buf[len], "tx DMA Ring Offset\t\t%d\n", (unsigned int) pDevCtrl->txDma->unused[0]);

        len += sprintf(&buf[len], "\nrx DMA Channel Config\t\t%08x\n", (unsigned int) pDevCtrl->rxDma->cfg);
        len += sprintf(&buf[len], "rx DMA Intr Control/Status\t%08x\n", (unsigned int) pDevCtrl->rxDma->intStat);
        len += sprintf(&buf[len], "rx DMA Intr Mask\t\t%08x\n", (unsigned int) pDevCtrl->rxDma->intMask);
        //len += sprintf(&buf[len], "rx DMA Ring Offset\t\t%d\n", (unsigned int) pDevCtrl->rxDma->unused[0]);
        len += sprintf(&buf[len], "rx DMA controller_cfg\t\t%08x\n", (unsigned int) pDevCtrl->dmaRegs->controller_cfg);
        len += sprintf(&buf[len], "rx DMA Threshhold Lo\t\t%d\n", (unsigned int) pDevCtrl->dmaRegs->flowctl_ch1_thresh_lo);
        len += sprintf(&buf[len], "rx DMA Threshhold Hi\t\t%d\n", (unsigned int) pDevCtrl->dmaRegs->flowctl_ch1_thresh_hi);
        len += sprintf(&buf[len], "rx DMA Buffer Alloc\t\t%d\n", (unsigned int) pDevCtrl->dmaRegs->flowctl_ch1_alloc);
        break;

#if 0
/*----------------------------- SKBs --------------------------------*/
    case PROC_DUMP_SKB_1of6: /* skb buffer usage, 1 of 6 */
        bufcnt = 0;
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = 0; i < MAX_RX_BUFS/6; i++) {
            skb = pDevCtrl->skb_pool[i];
	     if (skb) {
                len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            	  if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                    bufcnt++;
	     	}
        }
        //len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
        break;

    case PROC_DUMP_SKB_2of6: /* skb buffer usage, part 2 of 6 */
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = MAX_RX_BUFS/6; i < 2*MAX_RX_BUFS/6; i++) {
            skb = pDevCtrl->skb_pool[i];
            len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                bufcnt++;
        }
        //len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
        break;

    case PROC_DUMP_SKB_3of6: /* skb buffer usage, part 3 of 6 */
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = MAX_RX_BUFS/3; i < MAX_RX_BUFS/2; i++) {
            skb = pDevCtrl->skb_pool[i];
            len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                bufcnt++;
        }
        //len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
        break;

    case PROC_DUMP_SKB_4of6: /* skb buffer usage, part 4 of 4 */
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = MAX_RX_BUFS/2; i < 2*MAX_RX_BUFS/3; i++) {
            skb = pDevCtrl->skb_pool[i];
            len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                bufcnt++;
        }
        //len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
        break;
		
    case PROC_DUMP_SKB_5of6: /* skb buffer usage, part 4 of 4 */
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = 2*MAX_RX_BUFS/3; i < 5*MAX_RX_BUFS/6; i++) {
            skb = pDevCtrl->skb_pool[i];
            len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                bufcnt++;
        }
        //len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
        break;
		
    case PROC_DUMP_SKB_6of6: /* skb buffer usage, part 4 of 4 */
        len += sprintf(&buf[len], "\nskb\taddress\t\tuser\tdataref\n");
        for (i = 5*MAX_RX_BUFS/6; i < MAX_RX_BUFS; i++) {
            skb = pDevCtrl->skb_pool[i];
            len += sprintf(&buf[len], "%d\t%08x\t%d\t%d\n",
                        i, (int)skb, atomic_read(&skb->users), atomic_read(skb_dataref(skb)));
            if ((atomic_read(&skb->users) == 2) && (atomic_read(skb_dataref(skb)) == 1))
                bufcnt++;
        }
        len += sprintf(&buf[len], "\nnumber rx buffer available %d\n", bufcnt);
	    bufcnt = 0; /* Reset */
        break;
#endif

    case PROC_DUMP_EMAC_REGISTERS: /* EMAC registers */
        len += sprintf(&buf[len], "\nEMAC registers\n");
        len += sprintf(&buf[len], "rx pDevCtrl->emac->rxControl\t\t%08x\n", (unsigned int) pDevCtrl->emac->rxControl);
        len += sprintf(&buf[len], "rx config register\t0x%08x\n", (int)pDevCtrl->emac->rxControl);
        len += sprintf(&buf[len], "rx max length register\t0x%08x\n", (int)pDevCtrl->emac->rxMaxLength);
        len += sprintf(&buf[len], "tx max length register\t0x%08x\n", (int)pDevCtrl->emac->txMaxLength);
        len += sprintf(&buf[len], "interrupt mask register\t0x%08x\n", (int)pDevCtrl->emac->intMask);
        len += sprintf(&buf[len], "interrupt register\t0x%08x\n", (int)pDevCtrl->emac->intStatus);
        len += sprintf(&buf[len], "control register\t0x%08x\n", (int)pDevCtrl->emac->config);
        len += sprintf(&buf[len], "tx control register\t0x%08x\n", (int)pDevCtrl->emac->txControl);
        len += sprintf(&buf[len], "tx watermark register\t0x%08x\n", (int)pDevCtrl->emac->txThreshold);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 0, (int)pDevCtrl->emac->pm0DataHi, (int)pDevCtrl->emac->pm0DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 1, (int)pDevCtrl->emac->pm1DataHi, (int)pDevCtrl->emac->pm1DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 2, (int)pDevCtrl->emac->pm2DataHi, (int)pDevCtrl->emac->pm2DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 3, (int)pDevCtrl->emac->pm3DataHi, (int)pDevCtrl->emac->pm3DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 4, (int)pDevCtrl->emac->pm4DataHi, (int)pDevCtrl->emac->pm4DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 5, (int)pDevCtrl->emac->pm5DataHi, (int)pDevCtrl->emac->pm5DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 6, (int)pDevCtrl->emac->pm6DataHi, (int)pDevCtrl->emac->pm6DataLo);
        len += sprintf(&buf[len], "pm%d\t0x%08x 0x%08x\n", 7, (int)pDevCtrl->emac->pm7DataHi, (int)pDevCtrl->emac->pm7DataLo);
        break;

    default:
        break;
    }

    return len;
}

static int dev_proc_engine(void *data, loff_t pos, char *buf)
{
    BcmEnet_devctrl *pDevCtrl = (BcmEnet_devctrl *)data;
    int len = 0;

    if (pDevCtrl == NULL)
        return len;

    if (pos >= PROC_DUMP_DMADESC_STATUS && pos <= PROC_DUMP_SKB_6of6) {
        len = bcmemac_net_dump(pDevCtrl, buf, pos);
    }
    return len;
}

/*
 *  read proc interface
 */
static ssize_t eth_proc_read(struct file *file, char *buf, size_t count,
        loff_t *pos)
{
    const struct inode *inode = file->f_dentry->d_inode;
    const struct proc_dir_entry *dp = PDE(inode);
    char *page;
    int len = 0, x, left;

    page = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!page)
        return -ENOMEM;
    left = PAGE_SIZE - 256;
    if (count < left)
        left = count;

    for (;;) {
        x = dev_proc_engine(dp->data, *pos, &page[len]);
        if (x == 0)
            break;
        if ((x + len) > left)
            x = -EINVAL;
        if (x < 0) {
            break;
        }
        len += x;
        left -= x;
        (*pos)++;
        if (left < 256)
            break;
    }
    if (len > 0 && copy_to_user(buf, page, len))
        len = -EFAULT;
    kfree(page);
    return len;
}

/*
 * /proc/driver/eth_rtinfo
 */
static struct file_operations eth_proc_operations = {
        read: eth_proc_read, /* read_proc */
};


#define BUFFER_LEN PAGE_SIZE


void bcmemac_dump(BcmEnet_devctrl *pDevCtrl)
{
    // We may have to do dynamic allocation here, since this is run from
    // somebody else's stack
    char buffer[BUFFER_LEN];
    int len, i;

    if (pDevCtrl == NULL) {
        return; // EMAC not initialized
    }

    /* First qtr of TX ring */
    printk("\ntx buffer descriptor ring status.\n");
    printk("BD\tlocation\tlength\tstatus addr\n");
    printk("%d TX buffers\n", pDevCtrl->nrTxBds);
    for (i = 0; i < pDevCtrl->nrTxBds; i++)
    {
        len += printk("%03d\t%08x\t%04ld\t%04lx %08lx\n",
                        i,(unsigned int)&pDevCtrl->txBds[i],
                        (pDevCtrl->txBds[i].length_status>>16),
                        (pDevCtrl->txBds[i].length_status&0xffff),
                        pDevCtrl->txBds[i].address);
    }

    printk("\nrx buffer descriptor ring status.\n");
    printk("BD\tlocation\tlength\tstatus\n");
    for (i = 0; i < pDevCtrl->nrRxBds; i++)
    {
        len += printk("%03d\t%08x\t%04ld\t%04lx %08lx\n",
                    i,(int)&pDevCtrl->rxBds[i],
                    (pDevCtrl->rxBds[i].length_status>>16),
                    (pDevCtrl->rxBds[i].length_status&0xffff),
                    pDevCtrl->rxBds[i].address);
    }

    /* DMA descriptor pointers and status */
    printk("\n\n=========== DMA descriptrs pointers and status ===========\n");
    len = bcmemac_net_dump(pDevCtrl, buffer, PROC_DUMP_DMADESC_STATUS);
    if (len >= BUFFER_LEN) printk("************ bcmemac_dump: ERROR: Buffer too small, PROC_DUMP_DMADESC_STATUS need %d\n", len);
    buffer[len] = '\0';
    printk("%s\n\n", buffer);

    /* EMAC Registers */
    printk("\n\n=========== EMAC Registers  ===========\n");
    len = bcmemac_net_dump(pDevCtrl, buffer, PROC_DUMP_EMAC_REGISTERS);
    if (len >= BUFFER_LEN) printk("************ bcmemac_dump: ERROR: Buffer too small, PROC_DUMP_EMAC_REGISTERS need %d\n", len);
    buffer[len] = '\0';
    printk("%s\n\n", buffer);

    printk("Other Debug info: Loop Count=%d, #rx_errors=%lu, #resets=%d, #overflow=%d,#NO_DESC=%d\n", 
            loopCount,  pDevCtrl->stats.rx_errors, gNumResetsErrors, gNumOverflowErrors, gNumNoDescErrors);
    printk("Last dma flag=%08x, last error dma flag = %08x, devInUsed=%d, linkState=%d\n",
            gLastDmaFlag, gLastErrorDmaFlag, /*gTimerScheduled, */atomic_read(&pDevCtrl->devInUsed), pDevCtrl->linkState);
}

#endif

EXPORT_SYMBOL(bcmemac_dump);


static void bcmemac_getMacAddr(struct net_device* dev)
{
    uint8 flash_eaddr[ETH_ALEN];
    void *virtAddr;
    uint16 word;
    int i;

#if !defined( CONFIG_BRCM_PCI_SLAVE) && !defined( CONFIG_MTD_BRCMNAND )
#if 1
    virtAddr = (void *)FLASH_MACADDR_ADDR;
#else
    virtAddr = (void*) KSEG1ADDR(getPhysFlashBase() + FLASH_MACADDR_OFFSET); 
#endif

    /* It is a common problem that the flash and/or Chip Select are
     * not initialized properly, so leave this printk on
     */
    printk("%s: Reading MAC address from %08lX, FLASH_BASE=%08lx\n", 
        dev->name,(uint32) virtAddr, (unsigned long) 0xA0000000L|getPhysFlashBase());

    word=0;
    word=readw(virtAddr);
    flash_eaddr[0]=(uint8) (word & 0x00FF);
    flash_eaddr[1]=(uint8) ((word & 0xFF00) >> 8);
    word=readw(virtAddr+2);
    flash_eaddr[2]=(uint8) (word & 0x00FF);
    flash_eaddr[3]=(uint8) ((word & 0xFF00) >> 8);
    word=readw(virtAddr+4);
    flash_eaddr[4]=(uint8) (word & 0x00FF);
    flash_eaddr[5]=(uint8) ((word & 0xFF00) >> 8);

    printk("%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X fetched from addr %lX\n",
                dev->name,
                flash_eaddr[0],flash_eaddr[1],flash_eaddr[2],
                flash_eaddr[3],flash_eaddr[4],flash_eaddr[5],
                (uint32) virtAddr);

#elif defined( CONFIG_MTD_BRCMNAND )
{
	extern int gNumHwAddrs;
	extern unsigned char* gHwAddrs[];
	
   	if (gNumHwAddrs >= 1) {
		for (i=0; i < 6; i++) {
			flash_eaddr[i] = (uint8) gHwAddrs[0][i];
		}

		printk("%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X fetched from bootloader\n",
			dev->name,
			flash_eaddr[0],flash_eaddr[1],flash_eaddr[2],
			flash_eaddr[3],flash_eaddr[4],flash_eaddr[5]
			);
   	}
	else {
		printk(KERN_ERR "%s: No MAC addresses defined\n", __FUNCTION__);
	}
}

#else 
/* PCI slave cannot access the EBI bus, 
 * and for now, same for NAND flash, until CFE supports it
 */
/* Use hard coded value if Flash not properly initialized */
    {
        flash_eaddr[0] = 0x00;
        flash_eaddr[1] = 0xc0;
        flash_eaddr[2] = 0xa8;
        flash_eaddr[3] = 0x74;
        flash_eaddr[4] = 0x3b;
        flash_eaddr[5] = 0x51;
        printk("%s: Default MAC address %02X:%02X:%02X:%02X:%02X:%02X used\n",
                    dev->name,
                    flash_eaddr[0],flash_eaddr[1],flash_eaddr[2],
                    flash_eaddr[3],flash_eaddr[4],flash_eaddr[5]);
    }
#endif
    /* fill in the MAC address */
    for (i = 0; i < 6; i++) {
        dev->dev_addr[i] = flash_eaddr[i];
    }

    /* print the Ethenet address */
    printk("%s: MAC Address: ", dev->name);
    for (i = 0; i < 5; i++) {
        printk("%2.2X:", dev->dev_addr[i]);
    }
    printk("%2.2X\n", dev->dev_addr[i]);
}


static void bcmemac_dev_setup(struct net_device *dev)
{
    int ret;
    ETHERNET_MAC_INFO EnetInfo;
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
#ifdef USE_PROC
    struct proc_dir_entry *p;
    char proc_name[32];
#endif

    if (pDevCtrl == NULL) {
        printk((KERN_ERR CARDNAME ": unable to allocate device context\n"));
        return;
    }

    eth_root_dev = dev;
    /* initialize the context memory */
    memset(pDevCtrl, 0, sizeof(BcmEnet_devctrl));
    /* back pointer to our device */
    pDevCtrl->dev = dev;
    pDevCtrl->linkstatus_phyport = -1;

    spin_lock_init(&pDevCtrl->lock);
    atomic_set(&pDevCtrl->devInUsed, 0);

    /* figure out which chip we're running on */
#if defined( CONFIG_MIPS_BCM7038 )  || defined(CONFIG_MIPS_BCM7400) || \
    defined(CONFIG_MIPS_BCM7401)    || defined( CONFIG_MIPS_BCM7402 ) || \
    defined( CONFIG_MIPS_BCM7402S ) || defined( CONFIG_MIPS_BCM7440 ) || \
    defined(CONFIG_MIPS_BCM7403)    || defined( CONFIG_MIPS_BCM7452 )

    /* TBD : need to get the symbol for these. */
    pDevCtrl->chipId  = (*((volatile unsigned long*)0xb0000200) & 0xFFFF0000) >> 16;
    pDevCtrl->chipRev = (*((volatile unsigned long*)0xb0000208) & 0xFF);
#elif defined(CONFIG_MIPS_BCM7318) 
    pDevCtrl->chipId  = (INTC->RevID & 0xFFFF0000) >> 16;
    pDevCtrl->chipRev = (INTC->RevID & 0xFF);
#else
    #error "Unsupported platform"
#endif
    switch (pDevCtrl->chipId) {
    case 0x7110:
    case 0x7038:
    case 0x7318:
    case 0x7401:
    case 0x7400:
    case 0x7440:
    case 0x7438:
    case 0x7403:
        break;
    default:
        printk(KERN_DEBUG CARDNAME" not found\n");
        return;
    }
    /* print the ChipID and module version info */
    printk("Broadcom BCM%X%X Ethernet Network Device ", pDevCtrl->chipId, pDevCtrl->chipRev);
    printk(VER_STR "\n");

    if( BpGetEthernetMacInfo( &EnetInfo) != BP_SUCCESS ) {
        printk(KERN_DEBUG CARDNAME" board id not set\n");
        return;
    }
    memcpy(&(pDevCtrl->EnetInfo), &EnetInfo, sizeof(ETHERNET_MAC_INFO));

    if ((pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH) &&
        (pDevCtrl->EnetInfo.usManagementSwitch == BP_ENET_MANAGEMENT_PORT)) {
            pDevCtrl->bIPHdrOptimize = FALSE;
        } else {
            pDevCtrl->bIPHdrOptimize = haveIPHdrOptimization();
        }

    bcmemac_getMacAddr(dev);

    if ((ret = bcmemac_init_dev(pDevCtrl))) {
        bcmemac_uninit_dev(pDevCtrl);
        eth_root_dev = NULL;
        return;
    }
#ifdef USE_PROC
    /* create a /proc entry for display driver runtime information */
    sprintf(proc_name, PROC_ENTRY_NAME);
    if ((p = create_proc_entry(proc_name, 0, NULL)) == NULL) {
        printk((KERN_ERR CARDNAME ": unable to create proc entry!\n"));
        bcmemac_uninit_dev(pDevCtrl);
        eth_root_dev = NULL;
        return;
    }
    p->proc_fops = &eth_proc_operations;
    p->data = pDevCtrl;

    if (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH)
        ethsw_add_proc_files(dev);
#endif


    /* setup the rx irq */
    /* register the interrupt service handler */
    /* At this point dev is not initialized yet, so use dummy name */
#ifdef CONFIG_TIVO
    request_irq(pDevCtrl->rxIrq, bcmemac_net_isr, SA_INTERRUPT|SA_SHIRQ|IRQF_SAMPLE_RANDOM, dev->name, pDevCtrl);
#else
    request_irq(pDevCtrl->rxIrq, bcmemac_net_isr, SA_INTERRUPT|SA_SHIRQ, dev->name, pDevCtrl);
#endif

#ifdef USE_BH
    pDevCtrl->task.routine = bcmemac_rx;
    pDevCtrl->task.data = (void *)pDevCtrl;
#endif
    /*
     * Setup the timer
     */
    init_timer(&pDevCtrl->timer);
    pDevCtrl->timer.data = (unsigned long)pDevCtrl;
    pDevCtrl->timer.function = tx_reclaim_timer;

#ifdef DUMP_DATA
    printk(KERN_INFO CARDNAME ": CPO BRCMCONFIG: %08X\n", read_c0_diag(/*$22*/));
    printk(KERN_INFO CARDNAME ": CPO MIPSCONFIG: %08X\n", read_c0_config(/*$16*/));
    printk(KERN_INFO CARDNAME ": CPO MIPSSTATUS: %08X\n", read_c0_status(/*$12*/));
#endif	
    dev->irq                    = pDevCtrl->rxIrq;
    dev->base_addr              = ENET_MAC_ADR_BASE;
    dev->open                   = bcmemac_net_open;
    dev->stop                   = bcmemac_net_close;
    dev->hard_start_xmit        = bcmemac_net_xmit;
    dev->tx_timeout             = bcmemac_net_timeout;
    dev->watchdog_timeo         = 2*HZ;
    dev->get_stats              = bcmemac_net_query;
    dev->set_mac_address        = bcm_set_mac_addr;
    dev->set_multicast_list     = bcm_set_multicast_list;
    dev->do_ioctl               = &bcmemac_enet_ioctl;
#ifdef CONFIG_NET_POLL_CONTROLLER
    dev->poll_controller        = &bcmemac_poll;
#endif
#ifdef VPORTS
    if ((pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH) &&
        (pDevCtrl->EnetInfo.usManagementSwitch == BP_ENET_MANAGEMENT_PORT)) {
        dev->header_cache_update    = NULL;
        dev->hard_header_cache      = NULL;
        dev->hard_header_parse      = NULL;
        dev->hard_header            = bcmemac_header;
        dev->hard_header_len        = ETH_HLEN + BRCM_TAG_LEN;
        set_vnet_dev(0, dev);
        ethsw_switch_unmanage_mode(vnet_dev[0]);
        ethsw_config_vlan(vnet_dev[0], VLAN_DISABLE, 0);
    }
#endif
    // These are default settings
    write_mac_address(dev);
    ether_setup(dev);

    // Let boot setup info overwrite default settings.
    netdev_boot_setup_check(dev);

    TRACE(("bcmemacenet: bcmemac_net_probe dev 0x%x\n", (unsigned int)dev));

    SET_MODULE_OWNER(dev);
}


/*
 *      bcmemac_net_probe: - Probe Ethernet switch and allocate device
 */
#ifdef MODULE
static
#endif
int __init bcmemac_net_probe(void)
{
    static int probed = 0;
    int ret;
    struct net_device *dev = NULL;
    BcmEnet_devctrl *pDevCtrl;

    if (probed == 0) {
#if defined(CONFIG_BCM5325_SWITCH) && (CONFIG_BCM5325_SWITCH == 1)
        if (BpSetBoardId("EXT_5325_MANAGEMENT") != BP_SUCCESS)
#else
        if (BpSetBoardId("INT_PHY") != BP_SUCCESS)
#endif
            return -ENODEV;

		dev = alloc_netdev(sizeof(struct BcmEnet_devctrl), "eth%d", bcmemac_dev_setup);
        if (dev == NULL) {
            printk(KERN_ERR CARDNAME": Unable to allocate net_device structure!\n");
            return -ENODEV;
        }
    } else {
        /* device has already been initialized */
        return -ENXIO;
    }

    pDevCtrl = (BcmEnet_devctrl *)netdev_priv(eth_root_dev);
    if (0 != (ret = register_netdev(dev))) {
        bcmemac_uninit_dev(pDevCtrl);
        eth_root_dev = NULL;
        return ret;
    }
#ifdef VPORTS
    if ((pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH) &&
        (pDevCtrl->EnetInfo.usManagementSwitch == BP_ENET_MANAGEMENT_PORT)) {
        ret = vnet_probe();
    }
#endif

#ifdef CONFIG_BCMINTEMAC_NETLINK
	INIT_WORK(&pDevCtrl->link_change_task, (void (*)(void *))bcmemac_link_change_task, pDevCtrl);
        printk("init link_change_task\n");
#endif

    return ret;
}

/* get ethernet port's status; return nonzero for Link up, 0 for Link down */
static int bcmIsEnetUp(struct net_device *dev)
{
    static int sem = 0;
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
    int linkState = pDevCtrl->linkState;

    if( sem == 0 )
    {
        sem = 1;

        if( (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH ) || (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_PHY))
        {
            /* Call non-blocking functions to read the link state. */
            if( pDevCtrl->linkstatus_phyport == -1 )
            {
                /* Start reading the link state of each switch port. */
                pDevCtrl->linkstatus_phyport = 0;
                mii_linkstatus_start(dev, pDevCtrl->EnetInfo.ucPhyAddress |
                    pDevCtrl->linkstatus_phyport);
            }
            else
            {
                int ls = 0;

                /* Check if the link state is done being read for a particular
                 * switch port.
                 */
                if( mii_linkstatus_check(dev, &ls) )
                {
                    /* The link state for one switch port has been read.  Save
                     * it in a temporary holder.
                     */
                    pDevCtrl->linkstatus_holder |=
                        ls << pDevCtrl->linkstatus_phyport++;

                    while (pDevCtrl->linkstatus_phyport < sizeof(pDevCtrl->phy_port)/sizeof(int))
                    {
                        if (pDevCtrl->phy_port[pDevCtrl->linkstatus_phyport])
                            break;
                        else
                            pDevCtrl->linkstatus_phyport++;
                    }
                    if( pDevCtrl->linkstatus_phyport ==
                        sizeof(pDevCtrl->phy_port)/sizeof(int))
                    {
                        /* The link state for all switch ports has been read.
                         * Return the current link state.
                         */
                        linkState = pDevCtrl->linkstatus_holder;
                        pDevCtrl->linkstatus_holder = 0;
                        pDevCtrl->linkstatus_polltimer = 0;
                        pDevCtrl->linkstatus_phyport = -1;
                    }
                    else
                    {
                        /* Start to read the link state for the next switch
                         * port.
                         */
                        mii_linkstatus_start(dev,
                            pDevCtrl->EnetInfo.ucPhyAddress |
                            pDevCtrl->linkstatus_phyport);
                    }
                }
                else
                    if( pDevCtrl->linkstatus_polltimer > (3 * POLLCNT_1SEC) )
                    {
                        /* Timeout reading MII status. Reset link state check. */
                        pDevCtrl->linkstatus_holder = 0;
                        pDevCtrl->linkstatus_polltimer = 0;
                        pDevCtrl->linkstatus_phyport = -1;
                    }
            }
        }
        else
        {
            linkState = mii_linkstatus(dev, pDevCtrl->EnetInfo.ucPhyAddress);
            pDevCtrl->linkstatus_polltimer = 0;
        }

        sem = 0;
    }

    return linkState;
}

/*
 * Generic cleanup handling data allocated during init. Used when the
 * module is unloaded or if an error occurs during initialization
 */
static void bcmemac_init_cleanup(struct net_device *dev)
{
    TRACE(("%s: bcmemac_init_cleanup\n", dev->name));

    unregister_netdev(dev);
#ifdef USE_PROC
    remove_proc_entry("driver/eth_rtinfo", NULL);
#endif
}

/* Uninitialize tx/rx buffer descriptor pools */
static int bcmemac_uninit_dev(BcmEnet_devctrl *pDevCtrl)
{
    Enet_Tx_CB *txCBPtr;
    char proc_name[32];
    int i;

    if (pDevCtrl) {
        /* disable DMA */
        pDevCtrl->txDma->cfg = 0;
        pDevCtrl->rxDma->cfg = 0;

        /* free the irq */
        if (pDevCtrl->rxIrq)
        {
            disable_irq(pDevCtrl->rxIrq);
            free_irq(pDevCtrl->rxIrq, pDevCtrl);
        }

        /* free the skb in the txCbPtrHead */
        while (pDevCtrl->txCbPtrHead)  {
            pDevCtrl->txFreeBds += pDevCtrl->txCbPtrHead->nrBds;

            if(pDevCtrl->txCbPtrHead->skb)
                dev_kfree_skb (pDevCtrl->txCbPtrHead->skb);

            txCBPtr = pDevCtrl->txCbPtrHead;

            /* Advance the current reclaim pointer */
            pDevCtrl->txCbPtrHead = pDevCtrl->txCbPtrHead->next;

            /* Finally, return the transmit header to the free list */
            txCb_enq(pDevCtrl, txCBPtr);
        }

        bcmemac_init_cleanup(pDevCtrl->dev);
        /* release allocated receive buffer memory */
        for (i = 0; i < MAX_RX_BUFS; i++) {
            if (pDevCtrl->skb_pool[i] != NULL) {
                dev_kfree_skb (pDevCtrl->skb_pool[i]);
                pDevCtrl->skb_pool[i] = NULL;
            }
        }
        /* free the transmit buffer descriptor */
        if (pDevCtrl->txBds) {
            pDevCtrl->txBds = NULL;
        }
        /* free the receive buffer descriptor */
        if (pDevCtrl->rxBds) {
            pDevCtrl->rxBds = NULL;
        }
        /* free the transmit control block pool */
        if (pDevCtrl->txCbs) {
            kfree(pDevCtrl->txCbs);
            pDevCtrl->txCbs = NULL;
        }
#ifdef USE_PROC
        sprintf(proc_name, PROC_ENTRY_NAME);
        remove_proc_entry(proc_name, NULL);
        if (pDevCtrl->EnetInfo.ucPhyType == BP_ENET_EXTERNAL_SWITCH)
          ethsw_del_proc_files();
#endif
        if (pDevCtrl->dev) {
            if (pDevCtrl->dev->reg_state != NETREG_UNINITIALIZED)
                unregister_netdev(pDevCtrl->dev);
            free_netdev(pDevCtrl->dev);
        }
    }
   
    return 0;
}

static void __exit bcmemac_module_cleanup(void)
{
    BcmEnet_devctrl *pDevCtrl;

    if (eth_root_dev) {
        pDevCtrl = (BcmEnet_devctrl *)netdev_priv(eth_root_dev);
        if (pDevCtrl) {
#ifdef VPORTS
            vnet_module_cleanup();
#endif
            bcmemac_uninit_dev(pDevCtrl);
        }
        eth_root_dev = NULL;
    }
    TRACE(("bcmemacenet: bcmemac_module_cleanup\n"));

}

static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
    u32 ethcmd;
    
#ifdef VPORTS
    if (is_vport(dev)) {
        pDevCtrl = netdev_priv(vnet_dev[0]);
    }
#endif

    ASSERT(pDevCtrl != NULL);   
        
    if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
        return -EFAULT;
    
    switch (ethcmd) {
    /* get driver-specific version/etc. info */
    case ETHTOOL_GDRVINFO: {
        struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};

        strncpy(info.driver, CARDNAME, sizeof(info.driver)-1);
        strncpy(info.version, VERSION, sizeof(info.version)-1);
        if (copy_to_user(useraddr, &info, sizeof(info)))
            return -EFAULT;
        return 0;
        }
    default:
        break;
    }
    
    return -EOPNOTSUPP;    
}   

static int bcmemac_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    BcmEnet_devctrl *pDevCtrl = netdev_priv(dev);
    struct mii_ioctl_data *mii;
    unsigned long flags;
    int val = 0;

#ifdef VPORTS
    if (is_vport(dev)) {
        pDevCtrl = netdev_priv(vnet_dev[0]);
    }
#endif

    /* we can add sub-command in ifr_data if we need to in the future */
    switch (cmd)
    {
    case SIOCGMIIPHY:       /* Get address of MII PHY in use. */
        mii = (struct mii_ioctl_data *)&rq->ifr_data;
        mii->phy_id = pDevCtrl->EnetInfo.ucPhyAddress;

    case SIOCGMIIREG:       /* Read MII PHY register. */
        spin_lock_irqsave(&pDevCtrl->lock, flags);
        mii = (struct mii_ioctl_data *)&rq->ifr_data;
        mii->val_out = mii_read(dev, mii->phy_id & 0x1f, mii->reg_num & 0x1f);
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        break;

    case SIOCSMIIREG:       /* Write MII PHY register. */
        spin_lock_irqsave(&pDevCtrl->lock, flags);
        mii = (struct mii_ioctl_data *)&rq->ifr_data;
        mii_write(dev, mii->phy_id & 0x1f, mii->reg_num & 0x1f, mii->val_in);
        spin_unlock_irqrestore(&pDevCtrl->lock, flags);
        break;

    case SIOCETHTOOL:
        return netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);
#ifdef CONFIG_TIVO
        break;

    case 0x89FF: /* SIOCTIVOGDEVINFO */
        {
            struct tivo_net_dev_info {
                unsigned long  isRemovable;
                unsigned short vendorId;
                unsigned short productId;
            } devinfo;
            devinfo.isRemovable = 0;
            devinfo.vendorId  = 0;
            devinfo.productId = 0;

            if (copy_to_user(rq->ifr_data, &devinfo, sizeof(devinfo))) {
                val = -EFAULT;
            }
        }
        break;
    default:
        val = -EPERM;
        break;

#endif /* CONFIG_TIVO */
    }

    return val;       
}

int __init bcmemac_module_init(void)
{
    int status;

    TRACE(("bcmemacenet: bcmemac_module_init\n"));
    status = bcmemac_net_probe();

    return status;
}

//ML 08062007 return the ethernet connection status
int isEnetConnected(char *pn)
{
    return bcmIsEnetUp(bcmemac_get_device());
}


#if defined(MODULE)
/*
 * Linux module entry.
 */
int init_module(void)
{
    return( bcmemac_module_init() );
}

/*
 * Linux module exit.
 */
void cleanup_module(void)
{
    if (MOD_IN_USE)
        printk(KERN_DEBUG CARDNAME" module is in use.  Cleanup is delayed.\n");
    else
        bcmemac_module_cleanup();
}
#endif

EXPORT_SYMBOL(bcmemac_get_free_txdesc);
EXPORT_SYMBOL(bcmemac_get_device);
EXPORT_SYMBOL(bcmemac_xmit_multibuf);
EXPORT_SYMBOL(bcmemac_xmit_check);

EXPORT_SYMBOL(isEnetConnected);

#if !defined(MODULE)
module_init(bcmemac_module_init);
module_exit(bcmemac_module_cleanup);
#endif

