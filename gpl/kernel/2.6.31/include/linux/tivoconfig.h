#ifndef _LINUX_TIVOCONFIG_H
#define _LINUX_TIVOCONFIG_H

/**
 * @file tivoconfig.h
 *
 * \#include <linux/tivoconfig.h>
 *
 * TiVo configuration registry
 *
 * Copyright (C) 2001-2004, 2007 TiVo Inc. All Rights Reserved.
 */

/* #include <linux/config.h> // DEPRECATED in 2.6.19 */

#define kTivoConfigMaxNumSelectors 64

typedef unsigned long TivoConfigSelector;
typedef unsigned long TivoConfigValue;
typedef int           TStatus;

#ifdef CONFIG_TIVO

#ifdef __KERNEL__

/**
 * Initialize the configuration registry from the static array of initializers
 *
 * NOTE: Must be called early at boot time to initialize contigmem parameters.
 * Core kernel only.
 *
 * @param value the ::TivoConfigBoardID to search the initialization array
 * for, usually with low 8 bits masked off
 */
void    InitTivoConfig( TivoConfigValue value );

#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Look up a value in the configuration registry
 *
 * @return 0 if found, -1 if not found
 * @param selector the configuration parameter to look up. See ::TivoConfigSelectors
 * @param value    the looked up configuration value, if found
 */
TStatus GetTivoConfig( TivoConfigSelector selector, TivoConfigValue *value );

/**
 * Set a value in the configuration registry, creating a new parameter in
 * the registry if necessary
 *
 * @return 0 if set, -ENOMEM if out of space in the registry, -1 if not
 * supported
 * @param selector the configuration parameter to set. See ::TivoConfigSelectors
 * @param value    the configuration value to set
 */
TStatus SetTivoConfig( TivoConfigSelector selector, TivoConfigValue  value );

#ifdef __cplusplus
}
#endif

#else // stub functions for systems without a config registry

static inline TStatus GetTivoConfig( TivoConfigSelector selector, TivoConfigValue *value )
{
	(void)selector;
	(void)value;
	return -1;
}

static inline TStatus SetTivoConfig( TivoConfigSelector selector, TivoConfigValue  value )
{
	(void)selector;
	(void)value;
	return -1;
}

#endif


/**
 * Selectors for the available configuration parameters
 * 
 * To create a new 32-bit selector, use:
 * <pre>
 *   echo -n BORD | od -t x1
 * </pre>
 * This is to avoid multi-char literals, which can have compiler-specific
 * byte ordering
 */
enum TivoConfigSelectors {

    /** 'BORD' Platform/PCB identifier. See ::TivoConfigBoardID below */
    kTivoConfigBoardID             = 0x424F5244,

    /** 'MEMS' Size of system memory, in bytes */
    kTivoConfigMemSize             = 0x4D454D53,

    /** 'I2CT' I2C bus type. See ::TivoConfigI2cType below */
    kTivoConfigI2cType             = 0x49324354,

    /** 'CMSZ' Total contigmem size, in bytes. Use specific region size where
     * possible, instead of looking at the total. Note that the total can be
     * overridden via kernel commandline param, which will add to the last
     * region present in the config table
     */
    kTivoConfigContigmemSize       = 0x434d535a,
    
    /** 'CMS0' Contigmem region 0 size */
    kTivoConfigContigmemRegion0    = 0x434d5330,

    /** 'CMS1' Contigmem region 1 size */
    kTivoConfigContigmemRegion1    = 0x434d5331,

    /** 'CMS2' Contigmem region 2 size */
    kTivoConfigContigmemRegion2    = 0x434d5332,

    /** 'CMS3' Contigmem region 3 size */
    kTivoConfigContigmemRegion3    = 0x434d5333,

    /** 'CMS4' Contigmem region 4 size */
    kTivoConfigContigmemRegion4    = 0x434d5334,

    /** 'CMS5' Contigmem region 5 size */
    kTivoConfigContigmemRegion5    = 0x434d5335,

    /** 'CMS6' Contigmem region 6 size */
    kTivoConfigContigmemRegion6    = 0x434d5336,

    /** 'CMS7' Contigmem region 7 size */
    kTivoConfigContigmemRegion7    = 0x434d5337,

    /** 'CMS8' Contigmem region 8 size */
    kTivoConfigContigmemRegion8    = 0x434d5338,

    /** 'CMS9' Contigmem region 9 size */
    kTivoConfigContigmemRegion9    = 0x434d5339,

    /** 'BM0S' bmem 0 size */
    kTivoConfigBmem0Size           = 0x424d3053,

    /** 'BM0A' bmem 0 address */
    kTivoConfigBmem0Address        = 0x424d3041,

    /** 'BM1S' bmem 1 size */
    kTivoConfigBmem1Size           = 0x424d3153,

    /** 'BM1A' bmem 1 address */
    kTivoConfigBmem1Address        = 0x424d3141,

    /** 'BCMV' BCM7020 chip version. DEPRICATED. See ::TivoConfigBcmVer below */
    kTivoConfigBcmVer              = 0x42434d56,

    /** 'IDEC' IDE capabilities. See ::TivoConfigIdeCap below */
    kTivoConfigIdeCap              = 0x49444543,

    /** 'FCHK' Fan check capability. See ::TivoConfigFanCheck below */
    kTivoConfigFanCheck            = 0x4643484b,

    /** 'IRMC' IR Microcontroller info. See ::TivoConfigIrMicro below */
    kTivoConfigIrMicro             = 0x49524d43,

    /** 'FPKY' Front Panel Key Scan info. See ::TivoConfigFPKeyScan below */
    kTivoConfigFPKeyScan           = 0x46504b59,

    /** 'TMPA' Temperature adjustment.
     * This parameter holds 2 fixed point 16-bit values: <pre>
     *  MSB: 8 bit signed integer part of scale
     *       8 bit fractional part of scale
     *       8 bit signed integer part of offset
     *  LSB: 8 bit fractional part of offset </pre>
     */
    kTivoConfigTempAdj             = 0x544d5041,

    /** 'FANC' Fan control.
     * This parameter holds the fan control parameters in units of degrees C
     * as reported by the therm driver (ie, after adjustment): <pre>
     *  MSB: 8 bit terminal temp
     *       8 bit critical temp
     *       8 bit log temp
     *  LSB: 8 bit target temp </pre>
     */
    kTivoConfigFanCtl              = 0x46414e43,

    /** 'FNC2' Alternate Fan control.
     * This is the second set of fan control parameters, with the
     * format and meaning as defined for FANC above. This second set
     * is optional, the first is to be used if the second is not
     * available.
     *
     * Used during DVD burning for example, while FANC are used when
     * not burning.
     */
    kTivoConfigFanCtl2             = 0x464e4332,

    /** 'FLSP' Fan Low SPeed
     * This is the lowest fan speed to be set on a box.
     */
    kTivoConfigFanLowSpeed         = 0x464c5350,

    /** 'NDIG' Number of digital tuners */
    kTivoConfigDigitalTunerCnt     = 0x4E444947,

    /** 'NANA' Number of analog  tuners  */
    kTivoConfigAnalogTunerCnt      = 0x4E414E41,

    /** 'APGS' APG Support Flag. See ::TivoConfigAPGSupport below */
    kTivoConfigAPGSupport          = 0x41504753,

    /** 'CAMS' CAM Support Flag. See ::TivoConfigCAMSupport below */
    kTivoConfigCAMSupport          = 0x43414D53,
    
    /** 'RSET' Software hard reset configuration. See ::TivoConfigReset below */
    kTivoConfigReset               = 0x52534554,

    /** 'DCOD' Video decoder type. Set in video input driver */
    kTivoConfigVideoDecoder        = 0x44434F44,

    /** 'NDVO' Number of digital video outputs (DVI or HDMI) */
    kTivoConfigDigitalVideoOutputCnt = 0x4E44564F,

    /** 'GSUR' Graphics surface memory, in bytes */
    kTivoConfigGraphicsSurfacePoolSize = 0x47535552,

};

#define MAX_CONTIGMEM_REGION 9

/**
 * 'BORD' Platform/PCB identifier
 *
 * <pre>
 *  MSB  8 bits identify OEM platform vendor (0x00 == TiVo)
 *      16 bits generally OEM vendor-specific, but should represent an
 *              enumeration of system config types
 *  LSB  8 bits hardware board revision (PCB) ID, if present </pre>
 */
enum TivoConfigBoardID {
//    kTivoConfigBoardIDClassic      = 0x00000100, /**< 0x000001 Series1 standalone */
//    kTivoConfigBoardIDCombo        = 0x00000200, /**< 0x000002 Series1 DirecTV combo */
//    kTivoConfigBoardIDBcmUma1a     = 0x00000300, /**< 0x000003 Series2 standalone 1 */
//    // reserved, do not use          0x00000400,
//    kTivoConfigBoardIDBcmUma2      = 0x00000500, /**< 0x000005 Series2 standalone 1 with ATA66 */
//    kTivoConfigBoardIDBcmUma1b     = 0x00000600, /**< 0x000006 Series2 standalone 1 with 73C shutoff */
//    kTivoConfigBoardIDBcmUma1c     = 0x00000700, /**< 0x000007 Series2 standalone 1 with 75C shutoff */
//    kTivoConfigBoardIDBcmUma1d     = 0x00000800, /**< 0x000008 Series2 standalone 1 with 65C shutoff */
//    kTivoConfigBoardIDBcmUma1e     = 0x00000900, /**< 0x000009 Series2 standalone 1 with 67C shutoff */
//    kTivoConfigBoardIDBcmUma1f     = 0x00000A00, /**< 0x00000A Series2 standalone 1 with 69C shutoff */
//    kTivoConfigBoardIDBcmUma3      = 0x00000B00, /**< 0x00000B Series2 DirecTV PX platform */
//    kTivoConfigBoardIDBcmUma4      = 0x00000C00, /**< 0x00000C Series2 DirecTV combo */
//    kTivoConfigBoardIDBcmUma5      = 0x00000D00, /**< 0x00000D Series2 standalone 2 */
//    kTivoConfigBoardIDBcmUma6      = 0x00000E00, /**< 0x00000E Series2 DirecTV combo w/ advanced security */
//    kTivoConfigBoardIDBcmUma7PX    = 0x00000F00, /**< 0x00000F Gen04 standalone 1 PX platform */
//    kTivoConfigBoardIDBcmUma7      = 0x00001000, /**< 0x000010 Gen04 standalone 1 */
//    // reserved, do not use          0x00001100,
//    kTivoConfigBoardIDElmoP0       = 0x00001200, /**< 0x000012 Elmo P0 platform */
//    kTivoConfigBoardIDOscar        = 0x00001300, /**< 0x000013 Oscar platform = Elmo/Grover w/o DV input */
//    kTivoConfigBoardIDBrycePY      = 0x00002000, /**< 0x000020 Gen04 DIRECTV PY */
//    kTivoConfigBoardIDBryce        = 0x00002100, /**< 0x000021 Gen04 DIRECTV */
//    kTivoConfigBoardIDDefiantPX1   = 0x00100000, /**< 0x001000 Series2 HD standalone PX-1 */
//    kTivoConfigBoardIDDefiantPX2   = 0x00100100, /**< 0x001001 Series2 HD standalone PX-2 */
//    kTivoConfigBoardIDDefiant      = 0x00100200, /**< 0x001002 Series2 HD standalone*/
//    kTivoConfigBoardIDPhoenixPX1   = 0x00100800, /**< 0x001008 Series2 HD DIRECTV combo  PX-1 */
//    kTivoConfigBoardIDPhoenixPX2   = 0x00100900, /**< 0x001009 Series2 HD DIRECTV combo  PX-2 */
//    kTivoConfigBoardIDPhoenix      = 0x00100A00, /**< 0x00100a Series2 HD DIRECTV combo*/
//    kTivoConfigBoardIDDreadnought  = 0x00101000, /**< 0x001010 Series2 HD megaboard */
//
//    // Gen05
//    kTivoConfigBoardIDEigerPX      = 0x00102000, /**< 0x001020 Eiger PX */
//    kTivoConfigBoardIDEiger        = 0x00102100, /**< 0x001021 Eiger P0 */
//    kTivoConfigBoardIDEigerP1a     = 0x00102300, /**< 0x001023 Eiger P1.5 */
//    kTivoConfigBoardIDEigerP2      = 0x00102400, /**< 0x001024 Eiger P2.0 */
//
//    // Gen06
//    kTivoConfigBoardIDFusionPX     = 0x00103000, /**< 0x001030 BCM 9740XX */
//    kTivoConfigBoardIDFusionP0     = 0x00103100, /**< 0x001031 Fusion P0  */
//    kTivoConfigBoardIDFusionP1     = 0x00103200, /**< 0x001032 Fusion P1  */
//    kTivoConfigBoardIDFusionMax    = 0x00103f00, /**< 0x00103f Fusion Max */
//    kTivoConfigBoardIDTroyPX       = 0x00104000, /**< 0x001040 Troy PX */
//    kTivoConfigBoardIDTroyP0       = 0x00104100, /**< 0x001041 Troy P0 */
//    kTivoConfigBoardIDTroy         = 0x00104200, /**< 0x001042 Troy */
//    kTivoConfigBoardIDTroyMax      = 0x00104f00, /**< 0x00104f Troy Max */
//    kTivoConfigBoardIDOldtronPX    = 0x00105000, /**< 0x001050 Neutron PX */
//    kTivoConfigBoardIDOldtronP0    = 0x00105100, /**< 0x001051 Neutron P0 */    
//    kTivoConfigBoardIDOldtronP1    = 0x00105200, /**< 0x001052 Neutron P1 */
//    kTivoConfigBoardIDOldtronMax   = 0x00105f00, /**< 0x00105f Neutron Max*/
//    
    // Gen07
    kTivoConfigBoardIDGen07Base    = 0x00106000,     
    kTivoConfigBoardIDNeutron      = 0x00106000, // Neutron is the first...
    kTivoConfigBoardIDGen07Max     = 0x00107f00, // allow up to 32 Gen07 spins  
//    
//    kTivoConfigBoardIDTivoMax      = 0x00FFFFFF, /**< 0x00FFFF Top of TiVo range <BR> <BR> */
//
//    kTivoConfigBoardIDSonyBase     = 0x01000000, /**< 0x010000 Start of Sony range */
//    // reserved, do not use        = 0x01000100, /**< 0x010001 Falcon platform 1 */
//    kTivoConfigBoardIDFalconUS1    = 0x01010100, /**< 0x010101 US-Falcon platform 1 */
//    kTivoConfigBoardIDSonyMax      = 0x01FFFFFF, /**< 0x01FFFF Top of Sony range <BR> <BR> */
//
//    kTivoConfigBoardIDTakaraBase   = 0x02000000, /**< 0x020000 Start of Takara range */
//    kTivoConfigBoardIDTakaraPX     = 0x02000100, /**< 0x020001 Takara PX */
//    kTivoConfigBoardIDTakaraP0     = 0x02000200, /**< 0x020002 Takara P0, genesis chip rev AB */
//    kTivoConfigBoardIDTakaraP1     = 0x02000300, /**< 0x020003 Takara P1, genesis chip rev AB */
//    kTivoConfigBoardIDTakaraP2     = 0x02000400, /**< 0x020004 Takara P2, genesis chip rev BC */
//    kTivoConfigBoardIDTakaraP2a    = 0x02000500, /**< 0x020005 Takara P2.5, includes USB fixes and RF out cleanup */
//    kTivoConfigBoardIDTakara       = 0x02000600, /**< 0x020006 Takara MR */
//    kTivoConfigBoardIDTakaraMax    = 0x02FFFFFF, /**< 0x02FFFF Top of Takara range <BR> <BR> */
//
//    kTivoConfigBoardIDToshibaBase  = 0x03000000, /**< 0x030000 Start of Toshiba range */        
//    kTivoConfigBoardIDToshibaPX    = 0x03000100, /**< 0x030001 Toshiba PX */        
//    kTivoConfigBoardIDToshibaP0    = 0x03000200, /**< 0x030002 Toshiba P0 */        
//    kTivoConfigBoardIDToshibaMax   = 0x03FFFFFF, /**< 0x03FFFF End of Toshiba range */        
//
//    kTivoConfigBoardIDToshSemiBase = 0x04000000, /**< 0x040000 Start of Toshiba Semiconductor range */
//    kTivoConfigBoardIDToshSemiPX   = 0x04000100, /**< 0x040001 The reference design */
//    kTivoConfigBoardIDToshSemiMax  = 0x04FFFFFF, /**< 0x04FFFF End of Toshiba Semiconductor range */
//    kTivoConfigBoardIDTGCBase      = 0x05000000, /**< 0x050000 Start of TGC range */
//    kTivoConfigBoardIDTGCLlamaBase = 0x05000100, /**< Start of Llama range */
//    kTivoConfigBoardIDTGCLlamaPX   = 0x05000100, /**< 0x050001 Llama PX */
//    kTivoConfigBoardIDTGCLlamaPY   = 0x05000200, /**< 0x050002 Llama PY */
//    kTivoConfigBoardIDTGCLlamaP0   = 0x05000300, /**< 0x050003 Llama P0 */
//    kTivoConfigBoardIDTGCLlamaP1   = 0x05000400, /**< 0x050004 Llama P1 */
//    kTivoConfigBoardIDTGCLlamaP2   = 0x05000500, /**< 0x050005 Llama P2 */
//    kTivoConfigBoardIDTGCLlamaP01  = 0x05000600, /**< 0x050006 Llama P01 */
//    kTivoConfigBoardIDTGCLlama     = 0x05000600, /**< 0x050006 Llama */
//    kTivoConfigBoardIDTGCLlamaMax  = 0x05000600, /**< 0x050006 End of Llama range */        
//    kTivoConfigBoardIDTGCGeminiBase = 0x05001100, /**< Start of Gemini range */
//    kTivoConfigBoardIDTGCGeminiPX   = 0x05001100, /**< 0x050011 Gemini PX */
//    kTivoConfigBoardIDTGCGeminiP0   = 0x05001200, /**< 0x050011 Gemini P0 */
//    kTivoConfigBoardIDTGCGemini     = 0x05001200, /**< 0x050012 Gemini */
//    kTivoConfigBoardIDTGCGeminiMax  = 0x05001200, /**< 0x050012 End of Gemini range */        
//    kTivoConfigBoardIDTGCGhidraBase = 0x05802100, /**< Start of Ghidra range */
//    kTivoConfigBoardIDTGCGhidra     = 0x05802200, /**< 0x058022 Ghidra */
//    kTivoConfigBoardIDTGCGhidraMax  = 0x05802200, /**< 0x058022 End of Ghidra range */        
//    kTivoConfigBoardIDTGCMax        = 0x05FFFFFF, /**< 0x05FFFF End of TGC range */

    kTivoConfigBoardIDKoherenceBase = 0x06000000, /** Koherence Base (Mike Minakami) */
    kTivoConfigBoardIDLeoBase      = 0x06000000, /* TiVo Leo */
    kTivoConfigBoardIDLeo          = 0x06000000, 
    kTivoConfigBoardIDLeoMax       = 0x06000F00, 
    kTivoConfigBoardIDCerberusBase = 0x06001000, /* TiVo 3tuner Neutron */
    kTivoConfigBoardIDCerberus     = 0x06001000, 
    kTivoConfigBoardIDCerberusMax  = 0x06001F00, 
    kTivoConfigBoardIDCyclopsBase  = 0x06002000, /* TiVo 3tuner Neutron */
    kTivoConfigBoardIDCyclops      = 0x06002000, 
    kTivoConfigBoardIDCyclopsMax   = 0x06002F00, 
    kTivoConfigBoardIDKoherenceMax = 0x06FFFFFF, /*  End of Koherence*/
    
    kTivoConfigBoardIDThomsonBase  = 0x07000000, /**< 0x060000 Start of Thomson range */
    kTivoConfigBoardIDMojave       = 0x07000100, /**< 0x060001 Thomson HR22 */
    kTivoConfigBoardIDThomsonMax   = 0x07FFFFFF, /**< 0x06FFFF End of Thomson range */
    
    kTivoConfigBoardIDCiscoBase    = 0x08000000, /**< 0x080000 Start of Cisco range */
    kTivoConfigBoardIDGimbal       = 0x08000100, /**< 0x080001 Compass Gimbal */
    kTivoConfigBoardIDPicasso      = 0x08000200, /**< 0x080002 Picasso (Compass/Gimbal derivative) */
    kTivoConfigBoardIDCiscoMax     = 0x08FFFFFF, /**< 0x08FFFF End of Cisco range */

    kTivoConfigBoardIDSamsungBase  = 0x09000000, /**< 0x090000 Start of Samsung range */
    kTivoConfigBoardIDYundo		   = 0x09000100, /**< 0x090001 Yundo */
    kTivoConfigBoardIDSamsungMax   = 0x09FFFFFF, /**< 0x09FFFF End of Samsung range */

//    kTivoConfigBoardIDUnknown128M  = 0xFFFFFC00, /**< 0xFFFFFC Unknown Platform with 128MB DRAM */
//    kTivoConfigBoardIDUnknown64M   = 0xFFFFFD00, /**< 0xFFFFFD Unknown Platform with 64MB DRAM */
//    kTivoConfigBoardIDUnknown32M   = 0xFFFFFE00, /**< 0xFFFFFE Unknown Platform with 32MB DRAM */
//    kTivoConfigBoardIDUnknown      = 0xFFFFFF00, /**< 0xFFFFFF Unknown Platform */
};

#define IS_BOARD(VAL, NM) (((VAL) & 0xFFFFFF00) >= (TivoConfigValue) kTivoConfigBoardID##NM##Base && ((VAL) & 0xFFFFFF00) <= (TivoConfigValue) kTivoConfigBoardID##NM##Max)

/**
 * 'I2CT' I2C bus type
 */
enum TivoConfigI2cType {
    kTivoConfigI2cTypeSingleMSBus  = 0x00000001, 
    kTivoConfigI2cTypeDualMSBus    = 0x00000002, 
    kTivoConfigI2cTypeDualMSBusHD  = 0x00000003, 
    kTivoConfigI2cTypeQuadMSBus    = 0x00000004, 
};

/**
 * 'BCMV' BCM7020 chip version.
 *
 * DEPRICATED: Don't rely on this being set correctly
 */
enum TivoConfigBcmVer {
    kTivoConfigBcmVer7020Cx        = 0x00000001, 
    kTivoConfigBcmVer7020RA0       = 0x00000002, 
    kTivoConfigBcmVer7020RBx       = 0x00000003, 
    kTivoConfigBcmVerUnknown       = 0xffffffff, 
};

/**
 * 'IDEC' IDE capabilities
 */
enum TivoConfigIdeCap {            // bitfield
    kTivoConfigIdeCapUdma          = 0x00000001, /**< Has UDMA */
    kTivoConfigIdeCapAta66         = 0x00000002, /**< Has ATA66 */
    kTivoConfigIdeCapSG            = 0x00000004, /**< Has scatter/gather DMA */
    kTivoConfigIdeCapScramble      = 0x00000008, /**< Has IDE scrambling */
    kTivoConfigIdeCapSATA          = 0x00000010, /**< Has support for Serial ATA */
};

/**
 * 'FCHK' Fan check capability
 */
enum TivoConfigFanCheck {
    kTivoConfigFanCheckNone        = 0x00000000, /**< Has no fan check hardware */
    kTivoConfigFanCheckPresent     = 0x00000001, /**< Has fan check hardware */
};

/**
 * 'IRMC' IR Microcontroller info
 */
enum TivoConfigIrMicro {
    kTivoConfigIrMicroNone         = 0x00000000, /**< Has no IR microcontroller */
    kTivoConfigIrMicroPresent      = 0x00000001, /**< Has IR microcontroller */
};

/**
 * 'FPKY' Front Panel Key Scan info
 */
enum TivoConfigFPKeyScan {
    kTivoConfigFPKeyScan4x4        = 0x00000000, /**< Has traditional 4x4 key matrix */
    kTivoConfigFPKeyScan5x3        = 0x00000001, /**< Has Toshiba 5x3 key matrix */
    kTivoConfigFPKeyScan5and6      = 0x00000002, /**< Has Broadcom 5 button navigation
                                                      next to channel up/down, vol up/down
						      menu and guide */
};

/**
 * 'APGS' APG Support Flag
 */
enum TivoConfigAPGSupport {
    kTivoConfigAPGSupportNone     = 0x00000000, /**< no APG support */
    kTivoConfigAPGSupportDTV30    = 0x00000001, /**< DirecTV 3.0 APG */
};

/**
 * 'CAMS' CAM Support Flag
 */
enum TivoConfigCAMSupport {
    kTivoConfigCAMSupportNone     = 0x00000000, /**< no CAM support */
    kTivoConfigCAMSupportDTV      = 0x00000001, /**< DirecTV nagra cam */
};

/**
 * 'RSET' Software hard reset configuration
 */
enum TivoConfigReset {
    kTivoConfigResetUnknown       = 0x00000000, /**< Unknown hard reset */
    kTivoConfigResetBcm7020Gpio0  = 0x00000001, /**< Reset on BCM7020 GPIO 0 */
    kTivoConfigResetTivoAsicGpio9 = 0x00000002, /**< Reset on TiVo ASIC GPIO 9 */
    kTivoConfigResetEigerPx       = 0x00000003, /**< Reset on Broadcom 7038 */
    kTivoConfigResetFusionPx      = 0x00000004, /**< Reset on Broadcom 7401 */
};

/**
 * 'DCOD' Video decoder type. Set in video input driver
 */
enum TivoConfigVideoDecoder {
    kTivoConfigVideoDecoderUnknown = 0x00000000,
    kTivoConfigVideoDecoder7114    = 0x00000001,
    kTivoConfigVideoDecoder7115    = 0x00000002,
};

#endif /* _LINUX_TIVOCONFIG_H */
