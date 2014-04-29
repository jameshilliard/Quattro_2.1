/*
 * TiVo initialization and setup code
 *
 * Copyright (C) 2012 TiVo Inc. All Rights Reserved.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tivoconfig.h>
#include <linux/string.h>
#include <asm/bootinfo.h>
#include <linux/module.h>

#include "../common/cmdline.h"

// Disable whitelist on this platform, allow anything
char *platform_whitelist[] __initdata = { NULL };

/* In arc/mips/brcmstb/memory.c */
extern int __init bmem_setup(char *str);

int __init tivo_init(void)
{
    TivoConfigValue size, address;
    char buf[64];
#ifdef CONFIG_TIVO_DEVEL
    char *value;
#endif

    InitTivoConfig((TivoConfigValue) kTivoConfigBoardIDYundo);

    /* if bmem is not configured from the cmdline */
    if (!cmdline_lookup(arcs_cmdline, "bmem="))
    {
        /* Get the bmem settings from tivoconfig and set */
        if ((GetTivoConfig(kTivoConfigBmem0Size, &size) == 0) && 
            (GetTivoConfig(kTivoConfigBmem0Address, &address) == 0))
        {
            sprintf(buf, "%lu@%lu", size, address);
            bmem_setup(buf);

            /* Second region to configure? */
            if ((GetTivoConfig(kTivoConfigBmem1Size, &size) == 0) && 
                (GetTivoConfig(kTivoConfigBmem1Address, &address) == 0))
            {
                sprintf(buf, "%lu@%lu", size, address);
                bmem_setup(buf);
            }
        } else
        {
            printk("Warning: No bmem settings properly configured via cmdline or tivoconfig\n");
        }
    }
    
    // Use /dev/ttyS0 as the default kernel console if none specified
    if (!cmdline_lookup(arcs_cmdline, "console=")) 
        cmdline_append(arcs_cmdline, CL_SIZE, "console=ttyS0,115200");

    if (!cmdline_lookup(arcs_cmdline, "HpkImpl=")) 
        cmdline_append(arcs_cmdline, CL_SIZE, "HpkImpl=Gen07S");

#ifdef CONFIG_TIVO_DEVEL
    // this really ought to be cooked into the ipconfig code...
    if (!cmdline_lookup(arcs_cmdline, "ip=")) 
        cmdline_append(arcs_cmdline, CL_SIZE, "ip=dhcp");

    if (cmdline_lookup(arcs_cmdline, "nfs1"))
    {
        cmdline_delete(arcs_cmdline, "nfs1", NULL);
        cmdline_append(arcs_cmdline, CL_SIZE, "xnfsinit=192.168.1.250:/Gen07S/nfsroot sysgen=true rw");
    }

    if ((value = cmdline_lookup(arcs_cmdline, "nfsinit=")) != NULL || 
        (value = cmdline_lookup(arcs_cmdline, "xnfsinit=")) != NULL)
    {
        // note, leave the [x]nfsinit command intact so it lands in the environment
        cmdline_delete(arcs_cmdline, "init=", "nfsroot=", NULL);
        cmdline_append(arcs_cmdline, CL_SIZE, "init=/devbin/nfsinit nfsroot=%s", value);
    }
#endif

#ifndef CONFIG_TIVO_DEVEL
    // Turns off Cable Modem serial port logging
    *((volatile unsigned int *)(0xb0404124)) &= 0x0fffffff;
    *((volatile unsigned int *)(0xb0404128)) &= 0xfffffff0;
#endif

    return 0;
}
