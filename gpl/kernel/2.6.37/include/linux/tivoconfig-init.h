#ifndef _LINUX_TIVOCONFIG_INIT_H
#define _LINUX_TIVOCONFIG_INIT_H

/*
 * Initialization array for the TiVo configuration registry
 *
 * Copyright 2011 TiVo Inc. All Rights Reserved.
 */

typedef struct {
	TivoConfigSelector selector;
	TivoConfigValue value;
} TivoConfig;


/* Gen07 */
static TivoConfig gGen07ConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDNeutron },
    { kTivoConfigMemSize, 0x1ffff000 }, // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x00000000 }, /* 0MB - input section now in contigmem8 */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    { kTivoConfigContigmemRegion8, 0x06000000 }, /*96.0MB */
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, /* 60.0 MiB */
    { 0,0 }
};

/* Gen07Cerberus */
static TivoConfig gCerberusConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDCerberus },
    { kTivoConfigMemSize, 0x1ffff000 }, // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x00000000 }, /* 0MB - input section now in contigmem8 */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    { kTivoConfigContigmemRegion8, 0x09600000 }, /* 150.0MB */
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, /* 60.0 MiB */
    { 0,0 }
};

/* Gen07Cyclops */
static TivoConfig gCyclopsConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDCyclops },
    { kTivoConfigMemSize, 0x1ffff000 }, // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x00000000 }, /* 0MB - input section now in contigmem8 */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    { kTivoConfigContigmemRegion8, 0x08D00000 }, /* 150.0MB */
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, /* 60.0 MiB */
    { 0,0 }
};

/* Mojave/Thompson HR22 */
static TivoConfig gMojaveConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDMojave },
    { kTivoConfigMemSize, 0x0ffff000 }, // 256 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x01800000 }, /*24.0MB */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    { kTivoConfigContigmemRegion8, 0x06000000 }, /*96.0MB */
    { 0,0 }
};

/* Gen07CGimbal */
static TivoConfig gGimbalConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDGimbal },
    { kTivoConfigMemSize, 0x1ffff000 }, // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x00000000 }, /* 0MB - input section now in contigmem8 */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    /* contigmem8 region replaced by usage of bmem= params */
    { kTivoConfigGraphicsSurfacePoolSize, 0x04200000 }, /* 66.0 MiB of Region8 */
    { kTivoConfigBmem0Size, 0x00B700000 },   /* First bmem region: Main nexus driver*/
    { kTivoConfigBmem0Address, 0x800000 }, /* bmem=183M@8M  */
    { kTivoConfigBmem1Size, 0x01100000 },    /* Second bmem region: Cable Modem */
    { kTivoConfigBmem1Address, 0x0EF00000 }, /* bmem=17M@239M */
    { 0,0 }
};

/* Gen07PPicasso */
static TivoConfig gPicassoConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDPicasso },
    { kTivoConfigMemSize, 0x1ffff000 }, // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion1, 0x00000000 }, /* 0MB - input section now in contigmem8 */
    { kTivoConfigContigmemRegion2, 0x00200000 }, /* 2.0MB */
    { kTivoConfigContigmemRegion3, 0x00527000 }, /* 5280kB */
    /* contigmem8 region replaced by usage of bmem= params */
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, /* 60.0 MiB of Region8 */
    { kTivoConfigBmem0Size, 0x00A300000 },   /* First bmem region: Main nexus driver*/
    { kTivoConfigBmem0Address, 0x04C00000 }, /* bmem=163M@76M  */
    { kTivoConfigBmem1Size, 0x01100000 },    /* Second bmem region: Cable Modem */
    { kTivoConfigBmem1Address, 0x0EF00000 }, /* bmem=17M@239M */
    { 0,0 }
};

/* Gen08 Leo P1 */
static TivoConfig gLeo8P1ConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDLeo8P1 },
    { kTivoConfigMemSize, 0x1ffff000 },             // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion2, 0x00200000 },    // 2.0MB for ?
    { kTivoConfigContigmemRegion3, 0x00527000 },    // 5280kB for ?
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, // 60.0 MiB 
    // use bmem from broadcom drivers
    { kTivoConfigBmem0Size, 0x09C00000 },   // First bmem region: Main nexus driver
    { kTivoConfigBmem0Address, 0x04000000 }, // bmem=156M@64M
    { kTivoConfigBmem1Size, 0x0CE00000 },   // Second bmem region
    { kTivoConfigBmem1Address, 0x20000000 }, // bmem=206M@512M
    { 0,0 }
};

/* Gen09 Titan */
static TivoConfig gGen09TitanConfigTable[] __initdata = {
    { kTivoConfigBoardID, kTivoConfigBoardIDGen09Titan },
    { kTivoConfigMemSize, 0x1ffff000 },             // 512 MB, minus one page WAR
    { kTivoConfigContigmemRegion2, 0x00200000 },    // 2.0MB for ?
    { kTivoConfigContigmemRegion3, 0x00527000 },    // 5280kB for ?
    { kTivoConfigGraphicsSurfacePoolSize, 0x03C00000 }, // 60.0 MiB
    // use bmem from broadcom drivers
    { kTivoConfigBmem0Size, 0x0C000000 },   // First bmem region: Main nexus driver
    { kTivoConfigBmem0Address, 0x04000000 }, // bmem=192M@64M
    { kTivoConfigBmem1Size, 0x0C000000 },   // Second bmem region
    { kTivoConfigBmem1Address, 0x20000000 }, // bmem=192M@512M
    { 0,0 }
};

static TivoConfig *gGlobalConfigTable[] __initdata = { 
    gGen07ConfigTable,
    gMojaveConfigTable,
    gCerberusConfigTable,
    gCyclopsConfigTable,
    gGimbalConfigTable,
    gPicassoConfigTable,
    gLeo8P1ConfigTable,
    gGen09TitanConfigTable,
    0
};

#else

#error "Nobody should ever #include <linux/tivoconfig-init.h> more than once!"

#endif /* _LINUX_TIVOCONFIG_INIT_H */
