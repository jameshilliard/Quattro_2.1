/*
 *
 *  drivers/mtd/brcmnand/bcm7xxx-nand.c
 *
    Copyright (c) 2005-2006 Broadcom Corporation                 
    
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 2 as
 published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    File: bcm7xxx-nand.c

    Description: 
    This is a device driver for the Broadcom NAND flash for bcm97xxx boards.
when	who what
-----	---	----
051011	tht	codings derived from OneNand generic.c implementation.

 * THIS DRIVER WAS PORTED FROM THE 2.6.18-7.2 KERNEL RELEASE
 */
 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>

#include <linux/mtd/partitions.h>

// For CFE partitions.
#include <asm/brcmstb/brcmstb.h>


#include <asm/io.h>
//#include <asm/mach/flash.h>

#include "brcmnand.h"
#include "brcmnand_priv.h"

#define PRINTK(...)
//#define PRINTK printk

#define DRIVER_NAME		"brcmnand"
#define DRIVER_INFO		"Broadcom STB NAND controller"

extern int dev_debug;

/* 
 * NUM_NAND_CS here is strictly based on the number of CS in the NAND registers
 * It does not have the same value as NUM_CS in brcmstb/setup.c
 * It is not the same as NAND_MAX_CS, the later being the bit fields found in NAND_CS_NAND_SELECT.
 */

/* # number of CS supported by EBI */
#ifdef BCHP_NAND_CS_NAND_SELECT_EBI_CS_7_SEL_MASK
/* Version < 3 */
#define NAND_MAX_CS    8

#elif defined(BCHP_NAND_CS_NAND_SELECT_EBI_CS_3_SEL_MASK)
/* 7420Cx */
#define NAND_MAX_CS    4
#else
/* 3548 */
#define NAND_MAX_CS 2
#endif

/* Number of CS seen by NAND */
#if CONFIG_MTD_BRCMNAND_VERSION >= CONFIG_MTD_BRCMNAND_VERS_3_3
#define NUM_NAND_CS			4

#else
#define NUM_NAND_CS			2
#endif

struct brcmnand_info {
	struct mtd_info		mtd;
	struct brcmnand_chip	brcmnand;
#ifdef CONFIG_MTD_PARTITIONS
	int			nr_parts;
	struct mtd_partition*	parts;
#endif
};
static struct brcmnand_info* gNandInfo[NUM_NAND_CS];

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probe_types[] = { "cmdlinepart", "RedBoot", NULL };
#endif

#if 0
/* No longer used */
void* get_brcmnand_handle(void)
{
	void* handle = &info->brcmnand;
	return handle;
}
#endif



int gNandCS[NAND_MAX_CS];
/* Number of NAND chips, only applicable to v1.0+ NAND controller */
int gNumNand = 0;
int gClearBBT = 0;
char gClearCET = 0;
uint32_t gNandTiming1[NAND_MAX_CS], gNandTiming2[NAND_MAX_CS];

static char *cmd = NULL;
static unsigned long t1[NAND_MAX_CS], t2[NAND_MAX_CS];
static int nt1, nt2;

module_param(cmd, charp, 0444);
MODULE_PARM_DESC(cmd, "Special command to run on startup: "
		      "rescan - rescan for bad blocks, update BBT; "
		      "showbbt - print out BBT contents; "
		      "erase - erase entire flash, except CFE, and rescan "
		        "for bad blocks; "
		      "eraseall - erase entire flash, including CFE, and "
		        "rescan for bad blocks; "
		      "clearbbt - erase BBT and rescan for bad blocks "
		        "(DANGEROUS, may lose MFG BI's); "
		      "showcet - show correctable error count; "
		      "resetcet - reset correctable error count to 0 and "
		        "table to all 0xff");
module_param_array(t1, ulong, &nt1, 0444);
MODULE_PARM_DESC(t1, "Comma separated list of NAND timing values 1, 0 for default value");
module_param_array(t2, ulong, &nt2, 0444);
MODULE_PARM_DESC(t2, "Comma separated list of NAND timing values 2, 0 for default value");

static void* gPageBuffer = NULL;

/* NO need, already done in __setup("nandcs",); */

#if 1 /* Already done in arch/mips/brcmstb/setup.c */
/*
 * Sort Chip Select array into ascending sequence, and validate chip ID
 * We have to sort the CS in order not to use a wrong order when the user specify
 * a wrong order in the command line.
 */
static int
brcmnand_sort_chipSelects(struct brcmnand_chip* chip)
{
  #ifdef BCHP_NAND_CS_NAND_SELECT
  	int i;
	//extern const int gMaxNandCS;
	
  	chip->numchips = 0;
 
	for (i=0; i < NAND_MAX_CS; i++) {
		if (BDEV_RD(BCHP_NAND_CS_NAND_SELECT) & (0x100 << i)) {
			chip->CS[chip->numchips] = i;
			chip->numchips++;
		}
	}
PRINTK("%s: NAND_SELECT=%08x, numchips=%d\n", 
	__FUNCTION__, (unsigned int) BDEV_RD(BCHP_NAND_CS_NAND_SELECT), chip->numchips);
	return 0;

  #else /* Architecture do not support multiple NAND_SELECT */
	chip->numchips = 1;
  	chip->CS[0] = 0;

	return 0;
	
  #endif // def BCHP_NAND_CS_NAND_SELECT

}

#endif

/*
 * Since v3.3 controller allocate the BBT for each flash device, we cannot use the entire flash, but 
 * have to reserve the end of the flash for the BBT, or ubiformat will corrupt the BBT.
 */
static void brcmnand_add_mtd_partitions(
			struct mtd_info *mtd,
		       struct mtd_partition *parts,
		       int nr_parts)
{
	uint64_t devSize = device_size(mtd);
	uint32_t bbtSize = brcmnand_get_bbt_size(mtd); 
	
	int i;

	//add_mtd_device(mtd);
	for (i=0; i<nr_parts; i++) {
		/* Adjust only partitions that specify full MTD size */
		if (MTDPART_SIZ_FULL == parts[i].size) {
			parts[i].size = devSize - bbtSize;
			printk(KERN_WARNING "Adjust partition %s size from entire device to %llx to avoid overlap with BBT reserved space\n",
				parts[i].name, parts[i].size);
			
		}
		/* Adjust partitions that overlap the BBT reserved area at the end of the flash */
		else if ((parts[i].offset + parts[i].size) > (devSize - bbtSize)) {
			uint64_t adjSize = devSize - bbtSize - parts[i].offset;
			
			printk(KERN_WARNING "Adjust partition %s size from %llx to %llx to avoid overlap with BBT reserved space\n",
				parts[i].name, parts[i].size, adjSize);
			parts[i].size = adjSize;
		}
	}
	add_mtd_partitions(mtd, parts, nr_parts);
}

/*
 * Since v3.3 controller allocate the BBT for each flash device, we cannot use the entire flash, but 
 * have to reserve the end of the flash for the BBT, or ubiformat will corrupt the BBT.
 */
static struct mtd_partition single_partition_map[] = {
	/* name			offset		size */
	{ "entire_device00",	0x00000000,	MTDPART_SIZ_FULL },
};
static void brcmnand_add_mtd_device(struct mtd_info *mtd, int csi)
{
	uint64_t devSize = device_size(mtd);
	uint32_t bbtSize = brcmnand_get_bbt_size(mtd);

	//add_mtd_device(mtd);
	single_partition_map[0].size = devSize - bbtSize;
	sprintf(single_partition_map[0].name, "entire_device%02d", csi);
	add_mtd_partitions(mtd, single_partition_map, 1);
}

static int __devinit brcmnanddrv_probe(struct platform_device *pdev)
{
	struct brcmnand_platform_data *cfg = pdev->dev.platform_data;
	static int csi; // Index into dev/nandInfo array
	int cs = cfg->chip_select;	// Chip Select
	//int i;
	int err = 0;
	//static int numCSProcessed = 0;
	//int lastChip;
	struct brcmnand_info* info;
	

//extern int dev_debug, gdebug;



		/* FOr now all devices share the same buffer */
	if (!gPageBuffer) {
#ifndef CONFIG_MTD_BRCMNAND_EDU
	
	gPageBuffer = kmalloc(sizeof(struct nand_buffers), GFP_KERNEL);

#else
	/* Align on 32B boundary for efficient DMA transfer */
	gPageBuffer = kmalloc(sizeof(struct nand_buffers) + 31, GFP_DMA);
		
#endif
	}
	
	if (!gPageBuffer) {
		return -ENOMEM;
	}

	
	//gPageBuffer = NULL;
	info = kmalloc(sizeof(struct brcmnand_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	gNandInfo[csi] = info;
	memset(info, 0, sizeof(struct brcmnand_info));


	/*
	 * Since platform_data does not send us the number of NAND chips, 
	 * (until it's too late to be useful), we have to count it
	 */
#ifdef BCHP_NAND_CS_NAND_SELECT
	{
		uint32_t nandSelect;
		
		/* Set NAND_CS_NAND_SELECT if not already set by CFE */
		nandSelect = BDEV_RD(BCHP_NAND_CS_NAND_SELECT);
		nandSelect |= 0x100 << cs;
		BDEV_WR(BCHP_NAND_CS_NAND_SELECT, nandSelect);
		
		if (0 == gNumNand) { /* First CS */
			brcmnand_sort_chipSelects(&info->brcmnand);
			gNumNand = info->brcmnand.numchips;
		}
		else  { /* Subsequent CS */
			brcmnand_sort_chipSelects(&info->brcmnand);
			info->brcmnand.numchips = gNumNand;
		}	
	}

#else
	/* Version 1.0 and earlier */
		info->brcmnand.numchips = gNumNand = 1;
#endif
	info->brcmnand.csi = csi;
	

	/* FOr now all devices share the same buffer */


#ifndef CONFIG_MTD_BRCMNAND_EDU
	info->brcmnand.buffers = (struct nand_buffers*) gPageBuffer;
#else
	/* Align on 32B boundary for efficient DMA transfer */
	info->brcmnand.buffers = (struct nand_buffers*) (((unsigned int) gPageBuffer+31) & (~31));
#endif
			
	info->brcmnand.numchips = gNumNand; 
	info->brcmnand.chip_shift = 0; // Only 1 chip
	info->brcmnand.priv = &info->mtd;


	info->mtd.name = dev_name(&pdev->dev);
	info->mtd.priv = &info->brcmnand;
	info->mtd.owner = THIS_MODULE;

/* Enable the following for a flash based bad block table */
	info->brcmnand.options |= NAND_USE_FLASH_BBT;

	//cs = cfg->chip_select;
	

	//brcmnand_sort_chipSelects(&info->brcmnand);

	// Each chip now will have its own BBT (per mtd handle)
	// Problem is we don't know how many CS's we get, until its too late
	if (brcmnand_scan(&info->mtd, cs, gNumNand)) {
		err = -ENXIO;
		goto out_free_info;
	}

PRINTK("Master size=%08llx\n", info->mtd.size);	

#ifdef CONFIG_MTD_PARTITIONS
	/* allow cmdlineparts to override the default map */
	err = parse_mtd_partitions(&info->mtd, part_probe_types,
		&info->parts, 0);
	if (err > 0) {
		info->nr_parts = err;
	} else {
		info->parts = cfg->nr_parts ? cfg->parts : NULL;
		info->nr_parts = cfg->nr_parts;
	}

	// Add MTD partition have a dependency on the BBT
	if (info->nr_parts) // Primary mtd
		brcmnand_add_mtd_partitions(&info->mtd, info->parts, info->nr_parts);
	else  // subsequent NAND only hold 1 partition, and is a brand new mtd device
		brcmnand_add_mtd_device(&info->mtd, csi);
#else
	brcmnand_add_mtd_device(&info->mtd, csi);
#endif

PRINTK("After add_partitions: Master size=%08llx\n", info->mtd.size);	
		
	dev_set_drvdata(&pdev->dev, info);

	csi++;

	return 0;


out_free_info:

	if (gPageBuffer)
		kfree(gPageBuffer);
	kfree(info);
	return err;
}

static int __devexit brcmnanddrv_remove(struct platform_device *pdev)
{
	struct brcmnand_info *info = dev_get_drvdata(&pdev->dev);
	//struct resource *res = pdev->resource;
	//unsigned long size = res->end - res->start + 1;

	dev_set_drvdata(&pdev->dev, NULL);

	if (info) {
		del_mtd_partitions(&info->mtd);

		brcmnand_release(&info->mtd);
		//release_mem_region(res->start, size);
		//iounmap(info->brcmnand.base);
		kfree(gPageBuffer);
		kfree(info);
	}

	return 0;
}

static struct platform_driver brcmnand_platform_driver = {
		.probe			= brcmnanddrv_probe,
		.remove			= __devexit_p(brcmnanddrv_remove),
		.driver			= {
		.name		= DRIVER_NAME,
	},
};

static struct resource brcmnand_resources[] = {
	[0] = {
		.name		= DRIVER_NAME,
		.start		= BPHYSADDR(BCHP_NAND_REG_START),
		.end			= BPHYSADDR(BCHP_NAND_REG_END) + 3,
		.flags		= IORESOURCE_MEM,
	},
};

static int __init brcmnanddrv_init(void)
{
	int ret;
	int csi;
	int ncsi;

	if (cmd) {
		if (strcmp(cmd, "rescan") == 0)
			gClearBBT = 1;
		else if (strcmp(cmd, "showbbt") == 0)
			gClearBBT = 2;
		else if (strcmp(cmd, "eraseall") == 0)
			gClearBBT = 8;
		else if (strcmp(cmd, "erase") == 0)
			gClearBBT = 7;
		else if (strcmp(cmd, "clearbbt") == 0)
			gClearBBT = 9;
		else if (strcmp(cmd, "showcet") == 0)
			gClearCET = 1;
		else if (strcmp(cmd, "resetcet") == 0)
			gClearCET = 2;
		else if (strcmp(cmd, "disablecet") == 0)
			gClearCET = 3;
		else
			printk(KERN_WARNING "%s: unknown command '%s'\n",
				__FUNCTION__, cmd);
	}

	ncsi = min(nt1, nt2);
	for (csi=0; csi<ncsi; csi++) {
		gNandTiming1[csi] = t1[csi];
		gNandTiming2[csi] = t2[csi];
	}

	printk (KERN_INFO DRIVER_INFO " (BrcmNand Controller)\n");
		ret = platform_driver_register(&brcmnand_platform_driver);
		if (ret >= 0)
			request_resource(&iomem_resource, &brcmnand_resources[0]);
	
	return 0;
}

static void __exit brcmnanddrv_exit(void)
{
	release_resource(&brcmnand_resources[0]);
	platform_driver_unregister(&brcmnand_platform_driver);
}

module_init(brcmnanddrv_init);
module_exit(brcmnanddrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ton Truong <ttruong@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NAND flash driver");

