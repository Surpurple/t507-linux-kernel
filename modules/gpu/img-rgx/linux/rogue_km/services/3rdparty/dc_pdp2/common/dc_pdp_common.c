/*************************************************************************/ /*!
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "dc_pdp.h"
#include "pdp2_regs.h"
#include "pdp2_mmu_regs.h"

#if defined(LINUX)
#include <linux/spinlock.h>
#include <linux/delay.h>
#endif

/*******************************************************************************
 * Debug/print macros
 ******************************************************************************/
#if defined(PDP_DEBUG)
    #define PDP_CHECKPOINT PDP_DEBUG_PRINT(" CP: - %s\n", __FUNCTION__);
    #define PDP_DEBUG_PRINT(fmt, ...) \
		DC_OSDebugPrintf(DBGLVL_WARNING, fmt, __VA_ARGS__)        
#else
    #define PDP_CHECKPOINT  do {} while(0)
	#define PDP_DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

#define PDP_ERROR_PRINT(fmt, ...) \
    DC_OSDebugPrintf(DBGLVL_ERROR, fmt, __VA_ARGS__)

#if defined(PLATO_DISPLAY_PDUMP)

#undef DC_OSWriteReg32
#define DC_OSWriteReg32 PDP_OSWriteHWReg32

#define polpr(base,reg,val,msk,cnt,intrvl) \
	plato_pdump_pol(base,reg,val,msk, DRVNAME); \
	do { \
		IMG_UINT32 polnum; \
		for (polnum = 0; polnum < cnt; polnum++) \
		{ \
			if ((DC_OSReadReg32(base, reg) & msk) == val) \
			{ \
				break; \
			} \
			DC_OSDelayus(intrvl * 1000); \
		} \
		if (polnum == cnt) \
		{ \
			PDP_DEBUG_PRINT(" Poll failed for register: 0x%08X", (unsigned int)reg); \
		} \
	} while (0)
#else
#define polpr(base,reg,val,msk,cnt,intrvl) \
	do { \
		IMG_UINT32 polnum; \
		for (polnum = 0; polnum < cnt; polnum++) \
		{ \
			if ((DC_OSReadReg32(base, reg) & msk) == val) \
			{ \
				break; \
			} \
			DC_OSDelayus(intrvl * 1000); \
		} \
		if (polnum == cnt) \
		{ \
			PDP_DEBUG_PRINT(" Poll failed for register: 0x%08X", (unsigned int)reg); \
		} \
	} while (0)
#endif // PDUMP

#define pol(base,reg,val,msk) polpr(base,reg,val,msk,10,10)

#if defined(PDP_DEBUG)
static DCPDP_BUFFER *gpsBuffer[3];
static DCPDP_DEVICE * gPdpDevice;
void PDPDebugCtrl(void)
{
    int k = 0;
    
    printk("\n------PDP Register dump -------\n");

    for (k = 0; k < PDP_NUMREG; k++)
    {
        PDP_DEBUG_PRINT(" Offset %x: %x\n", k*4, DC_OSReadReg32(gPdpDevice->pvPDPRegCpuVAddr, k * 4));
    }

    printk("\n------PDP MMU Reg dump --------\n");
    for (k = 0; k < PDP_BIF_VERSION_OFFSET; k += 4)
    {
        PDP_DEBUG_PRINT(" BIF Offset %x: %x\n", k, DC_OSReadReg32(gPdpDevice->pvPDPBifRegCpuVAddr, k));
    }
    /*
    
	static int i = 0;
	IMG_CPU_VIRTADDR pvBufferCpuVAddr;
	IMG_UINT32 *pui32Pixel;
	IMG_UINT32 ui32SizeInPixels;
	IMG_UINT32 j;

    gpsBuffer[2] = gPdpDevice->psSystemBuffer;

    for (b = 0; b < 3; b++)
    {	
    	pvBufferCpuVAddr = DC_OSMapPhysAddr(gpsBuffer[b]->sCpuPAddr, gpsBuffer[b]->ui32SizeInBytes);
    	if (pvBufferCpuVAddr != NULL)
    	{
    		pui32Pixel = (IMG_UINT32 *)pvBufferCpuVAddr;
    		ui32SizeInPixels = 10; //gpsBuffer[i]->ui32Width;// * gpsBuffer[i]->ui32Height;

    		for (j = 0; j < ui32SizeInPixels; j++)
    		{
    			DC_OSDebugPrintf(DBGLVL_WARNING, " - %s: Buffer %d Pixel[%u]: 0x%08X\n", __func__, b, j, pui32Pixel[j]);
    		}

    		DC_OSUnmapPhysAddr(pvBufferCpuVAddr, gpsBuffer[b]->ui32SizeInBytes);
    	}
    }
	i = (i + 1) % 2;
	*/
}
#endif /* PDP_DEBUG */

static IMG_BOOL BufferAcquireMemory(DCPDP_DEVICE *psDeviceData, DCPDP_BUFFER *psBuffer)
{
	IMG_UINT32 i;

	for (i = 0; i < psDeviceData->ui32BufferCount; i++)
	{
		if ((psDeviceData->ui32BufferUseMask & (1UL << i)) == 0)
		{
			psDeviceData->ui32BufferUseMask |= (1UL << i);

			psBuffer->sCpuPAddr.uiAddr = psDeviceData->sDispMemCpuPAddr.uiAddr + (i * psDeviceData->ui32BufferSize);

			psDeviceData->sPVRServicesFuncs.pfnPhysHeapCpuPAddrToDevPAddr(psDeviceData->psPhysHeap,
										      1,
										      &psBuffer->sDevPAddr,
										      &psBuffer->sCpuPAddr);
			if (psBuffer->sDevPAddr.uiAddr == psBuffer->sCpuPAddr.uiAddr)
			{
				/* Because the device and CPU addresses are the same we assume that we have an
				   incompatible PDP device address. Calculate the correct PDP device address. */
				psBuffer->sDevPAddr.uiAddr -= psDeviceData->sDispMemCpuPAddr.uiAddr;
			}
			return IMG_TRUE;
		}
	}
	return IMG_FALSE;
}

static void BufferReleaseMemory(DCPDP_DEVICE *psDeviceData, DCPDP_BUFFER *psBuffer)
{
	IMG_UINT64 uiOffset;

	DC_ASSERT(psBuffer->sCpuPAddr.uiAddr >= psDeviceData->sDispMemCpuPAddr.uiAddr);
	DC_ASSERT(psBuffer->sCpuPAddr.uiAddr < (psDeviceData->sDispMemCpuPAddr.uiAddr + psDeviceData->uiDispMemSize));

	uiOffset = psBuffer->sCpuPAddr.uiAddr - psDeviceData->sDispMemCpuPAddr.uiAddr;
	uiOffset = DC_OSDiv64(uiOffset, psDeviceData->ui32BufferSize);

	psDeviceData->ui32BufferUseMask &= ~(1UL << uiOffset);

	psBuffer->sDevPAddr.uiAddr = 0;
	psBuffer->sCpuPAddr.uiAddr = 0;
}

static void EnableVSyncInterrupt(DCPDP_DEVICE *psDeviceData)
{
	IMG_UINT32 ui32InterruptEnable;

	ui32InterruptEnable = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTENAB_OFFSET);
	SET_FIELD(ui32InterruptEnable, PDP_INTENAB_INTEN_VBLNK0, 1);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTENAB_OFFSET, ui32InterruptEnable);
}

static void DisableVSyncInterrupt(DCPDP_DEVICE *psDeviceData)
{
	IMG_UINT32 ui32InterruptEnable;

	ui32InterruptEnable = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTENAB_OFFSET);
	SET_FIELD(ui32InterruptEnable, PDP_INTENAB_INTEN_VBLNK0, 0);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTENAB_OFFSET, ui32InterruptEnable);
}

static void PDPUpdateRegisters(DCPDP_DEVICE *psDeviceData)
{
	IMG_UINT32 ui32Value = 0;
	DC_ASSERT(psDeviceData != NULL);    
	/* To be set when register settings should be adopted by the PDP */
	SET_FIELD(ui32Value, PDP_REGISTER_UPDATE_CTRL_REGISTERS_VALID, 0x01);
    /* To indicate whether or not the PDP should adopt the updated register settings during */
    SET_FIELD(ui32Value, PDP_REGISTER_UPDATE_CTRL_USE_VBLANK, 0x01);    
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_REGISTER_UPDATE_CTRL_OFFSET, ui32Value); // 0x00000003
}

static void PDPResetMMU(DCPDP_DEVICE * psDeviceData)
{
    IMG_UINT32 ui32Value = 0;
    
	/* Display pipeline software reset: Software reset. */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_PDP_RST, 0x01);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET , ui32Value); // 0x20000000

	ui32Value = 0;
	/* Display pipeline software reset: Normal operation. */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_PDP_RST, 0x00);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET , ui32Value); // 0x00000000
	
}

static INLINE void Flip(DCPDP_BUFFER *psBuffer)
{
	IMG_UINT32 ui32Value = 0;
	IMG_UINT32 ui32Width = psBuffer->psDeviceData->pasTimingData[psBuffer->psDeviceData->uiTimingDataIndex].ui32HDisplay;
	IMG_UINT32 ui32Height = psBuffer->psDeviceData->pasTimingData[psBuffer->psDeviceData->uiTimingDataIndex].ui32VDisplay;

	ui32Value = 0;
	SET_FIELD(ui32Value, PDP_GRPH1SIZE_GRPH1WIDTH, ui32Width - 1);
	SET_FIELD(ui32Value, PDP_GRPH1SIZE_GRPH1HEIGHT, ui32Height - 1);
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1SIZE_OFFSET, ui32Value);

	ui32Value = 0;
	SET_FIELD(ui32Value, PDP_GRPH1POSN_GRPH1XSTART, 0);
	SET_FIELD(ui32Value, PDP_GRPH1POSN_GRPH1YSTART, 0);
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1POSN_OFFSET, ui32Value);

	ui32Value = 0;
	switch (psBuffer->ePixelFormat)
	{
		//case IMG_PIXFMT_B5G6R5_UNORM: // XXX Not supported technically we only have R5G6B5
		//{
		//	SET_FIELD(ui32Value, PDP_GRPH1SURF_GRPH1PIXFMT, DCPDP_PIXEL_FORMAT_RGB565);
		//	break;
		//}
		//case IMG_PIXFMT_B8G8R8X8_UNORM: // XXX Not supported technically we only have RGBA8
		case IMG_PIXFMT_B8G8R8A8_UNORM:
		//case IMG_PIXFMT_R8G8B8A8_UNORM:
		//case IMG_PIXFMT_R8G8B8X8_UNORM:
		{
			SET_FIELD(ui32Value, PDP_GRPH1SURF_GRPH1PIXFMT, DCPDP_PIXEL_FORMAT_ARGB8);
			break;
		}
		default:
		{
			DC_OSDebugPrintf(DBGLVL_ERROR, " - Unrecognised pixel format\n");
			DC_ASSERT(0);
		}
	}
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1SURF_OFFSET, ui32Value);

	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1STRIDE_OFFSET,
			PLACE_FIELD(PDP_GRPH1STRIDE_GRPH1STRIDE, (psBuffer->ui32ByteStride >> PDP_GRPH1STRIDE_DIVIDE) - 1));

	DC_ASSERT(psBuffer);
	/* Check if the address is a correct 40-bit address */
	DC_ASSERT((psBuffer->sDevPAddr.uiAddr & 0xffffff0000000000) == 0);

	/* Write 32 lsb of the address to stream address register */
	ui32Value = 0;
	ui32Value = DC_OSReadReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1BASEADDR_OFFSET);
	ui32Value = (psBuffer->sDevPAddr.uiAddr & PDP_GRPH1BASEADDR_GRPH1BASEADDR_MASK);
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1BASEADDR_OFFSET, ui32Value);
    
	/* Write 8 msb of the address to address extension bits in the PDP MMU control register */
	ui32Value = DC_OSReadReg32(psBuffer->psDeviceData->pvPDPBifRegCpuVAddr, PDP_BIF_ADDRESS_CONTROL_OFFSET);
	SET_FIELD(ui32Value, PDP_BIF_ADDRESS_CONTROL_UPPER_ADDRESS_FIXED, (psBuffer->sDevPAddr.uiAddr >> 32));
	SET_FIELD(ui32Value, PDP_BIF_ADDRESS_CONTROL_MMU_ENABLE_EXT_ADDRESSING, 0x00);
	SET_FIELD(ui32Value, PDP_BIF_ADDRESS_CONTROL_MMU_BYPASS, 0x01);
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPBifRegCpuVAddr, PDP_BIF_ADDRESS_CONTROL_OFFSET, ui32Value);

	PDPUpdateRegisters(psBuffer->psDeviceData);

#if defined(VIRTUAL_PLATFORM)
	/* Kick pdp sim render */
	DC_OSDebugPrintf(DBGLVL_DEBUG, "%s:%d: Kicking PDP render\n", __func__, __LINE__);
	DC_OSWriteReg32(psBuffer->psDeviceData->pvPDPRegCpuVAddr, PDP_INTCLR_OFFSET, 0x11111111);
#endif
}

static INLINE void FlipConfigQueueAdvanceIndex(DCPDP_FLIP_CONTEXT *psContext, IMG_UINT32 *pui32Index)
{
	(*pui32Index)++;

	if (*pui32Index >= DCPDP_MAX_COMMANDS_INFLIGHT)
	{
		*pui32Index = 0;
	}
}

static INLINE IMG_BOOL FlipConfigQueueEmptyNoLock(DCPDP_FLIP_CONTEXT *psContext)
{
	return (IMG_BOOL)(psContext->ui32FlipConfigInsertIndex == psContext->ui32FlipConfigRemoveIndex);
}

static IMG_BOOL FlipConfigQueueEmpty(DCPDP_FLIP_CONTEXT *psContext)
{
	unsigned long ulSpinlockFlags;
	IMG_BOOL bFlipConfigQueueEmpty;

	DC_OSSpinLockIRQSave(&psContext->sSpinLock, ulSpinlockFlags);
	bFlipConfigQueueEmpty = FlipConfigQueueEmptyNoLock(psContext);
	DC_OSSpinUnlockIRQRestore(&psContext->sSpinLock, ulSpinlockFlags);

	return bFlipConfigQueueEmpty;
}

static void FlipConfigQueueAdd(DCPDP_FLIP_CONTEXT *psContext,
				   IMG_HANDLE hConfigData,
				   DCPDP_BUFFER *psBuffer,
				   PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
				   IMG_UINT32 ui32DisplayPeriod)
{
	DCPDP_FLIP_CONFIG *psFlipConfig;
	unsigned long ulSpinlockFlags;

	DC_OSSpinLockIRQSave(&psContext->sSpinLock, ulSpinlockFlags);

	/* Get the next inactive flip config and update the insert index */
	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigInsertIndex];
	FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigInsertIndex);

	DC_ASSERT(psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_INACTIVE);

	/* Fill out the flip config */
	psFlipConfig->psContext		= psContext;
	psFlipConfig->hConfigData	= hConfigData;
	psFlipConfig->psBuffer		= psBuffer;
	psFlipConfig->ui32DisplayPeriod	= ui32DisplayPeriod;
	psFlipConfig->pasSurfAttrib	= pasSurfAttrib;

	/* Should be updated last */
	psFlipConfig->eStatus		= DCPDP_FLIP_CONFIG_PENDING;

	DC_OSSpinUnlockIRQRestore(&psContext->sSpinLock, ulSpinlockFlags);
}

static void SetModeRegisters(DCPDP_DEVICE *psDeviceData)
{
	DCPDP_TIMING_DATA *psTimingData = &psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex];
	IMG_UINT32 ui32Value;

	DC_ASSERT(psDeviceData->psSystemBuffer);

    PDP_CHECKPOINT;

	ui32Value = 0;
	/* Memory refreshes enabled during horizontal and vertical blanking. */
	SET_FIELD(ui32Value, PDP_MEMCTRL_MEMREFRESH, 0x02);
	/* Specifies number of consecutive words issued from the current base address before moving onto the next. */
	/* Issues with NoC sending interleaved read responses to PDP require burst to be 1 */
	SET_FIELD(ui32Value, PDP_MEMCTRL_BURSTLEN, 0x01);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_MEMCTRL_OFFSET, ui32Value); 

	PDPResetMMU(psDeviceData);

	ui32Value = 0;
	/* XXX Test these inputs, this may be configured incorrectly */
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_OPMASK_R_OFFSET , 0x00000000);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_OPMASK_GB_OFFSET, 0x00000000);

	ui32Value = 0;
	/* Power down mode: Output valid sync signals, and substitute active data with black pixels. */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_POWERDN, 0x01);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET , ui32Value); // 0x10000000

	/* Clear all interrupt enable bits, so that none of them are enabled for any plane */
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTENAB_OFFSET  , 0x00000000);

	ui32Value = 0;
	/* Power down mode: Output valid sync signals, and substitute active data with black pixels. */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_POWERDN, 0x01);
	/* Controls Blanking signal polarity: Blanking signal is active low */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_BLNKPOL, 0x01);
	/* Controls Vertical Sync polarity: Vertical Sync is active low */
    /* VSync polarity is active high (0), should come from hdmi driver */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_VSPOL, psTimingData->ui8VSyncPolarity);
	SET_FIELD(ui32Value, PDP_SYNCCTRL_HSPOL, psTimingData->ui8HSyncPolarity);
    //SET_FIELD(ui32Value, PDP_SYNCCTRL_HSDIS, 1);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET , ui32Value); // 0x10000028

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VSYNC1_OFFSET,
			PLACE_FIELD(PDP_VSYNC1_VBPS, psTimingData->ui32VBackPorch) | PLACE_FIELD(PDP_VSYNC1_VT, psTimingData->ui32VTotal));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VSYNC2_OFFSET,
			PLACE_FIELD(PDP_VSYNC2_VAS, psTimingData->ui32VActiveStart) | PLACE_FIELD(PDP_VSYNC2_VTBS, psTimingData->ui32VTopBorder));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VSYNC3_OFFSET,
			PLACE_FIELD(PDP_VSYNC3_VFPS, psTimingData->ui32VFrontPorch) | PLACE_FIELD(PDP_VSYNC3_VBBS, psTimingData->ui32VBottomBorder));

	if (psTimingData->bReducedBlanking == IMG_TRUE)
	{
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VEVENT_OFFSET,
			PLACE_FIELD(PDP_VEVENT_VEVENT, psTimingData->ui32VBottomBorder + DCPDP_REDUCED_BLANKING_VEVENT) | 
			PLACE_FIELD(PDP_VEVENT_VFETCH, psTimingData->ui32VBackPorch / 2));
	}
	else
	{
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VEVENT_OFFSET,
			PLACE_FIELD(PDP_VEVENT_VEVENT, 0) | PLACE_FIELD(PDP_VEVENT_VFETCH, psTimingData->ui32VBackPorch));
	}

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_VDECTRL_OFFSET,
			PLACE_FIELD(PDP_VDECTRL_VDES, psTimingData->ui32VActiveStart) | PLACE_FIELD(PDP_VDECTRL_VDEF, psTimingData->ui32VBottomBorder));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_HSYNC1_OFFSET,
			PLACE_FIELD(PDP_HSYNC1_HBPS, psTimingData->ui32HBackPorch) | PLACE_FIELD(PDP_HSYNC1_HT, psTimingData->ui32HTotal));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_HSYNC2_OFFSET,
			PLACE_FIELD(PDP_HSYNC2_HAS, psTimingData->ui32HActiveStart) | PLACE_FIELD(PDP_HSYNC2_HLBS, psTimingData->ui32HLeftBorder));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_HSYNC3_OFFSET,
			PLACE_FIELD(PDP_HSYNC3_HFPS, psTimingData->ui32HFrontPorch) | PLACE_FIELD(PDP_HSYNC3_HRBS, psTimingData->ui32HRightBorder));

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_HDECTRL_OFFSET,
			PLACE_FIELD(PDP_HDECTRL_HDES, psTimingData->ui32HActiveStart) | PLACE_FIELD(PDP_HDECTRL_HDEF, psTimingData->ui32HRightBorder));

	/* Enable GRPH1 */
	DCPDPEnableMemoryRequest(psDeviceData, IMG_TRUE);

	ui32Value = 0;
	/* Set the global alpha to 1023, i.e. fully saturated */
	SET_FIELD(ui32Value, PDP_GRPH1GALPHA_GRPH1GALPHA, 0x03FF);
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1GALPHA_OFFSET, ui32Value); // 0x000003FF

    DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1_MEM_THRESH_OFFSET, 0x780f80f8);

	ui32Value = 0;
	/* Starts Sync Generator. */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_SYNCACTIVE, 0x01);
	/* Controls polarity of pixel clock: Pixel clock is inverted */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_CLKPOL, 0x01);
	/* Controls Blanking signal polarity: Blanking signal is active low */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_BLNKPOL, 0x01);
	/* Controls Vertical Sync polarity: Vertical Sync is active low */
	SET_FIELD(ui32Value, PDP_SYNCCTRL_VSPOL, psTimingData->ui8VSyncPolarity);
	SET_FIELD(ui32Value, PDP_SYNCCTRL_HSPOL, psTimingData->ui8HSyncPolarity);    
	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET, ui32Value); // 0x80000828
}

static IMG_UINT32 GetTimingIndex(DCPDP_DEVICE *psDeviceData, IMG_UINT32 width, IMG_UINT32 height)
{
	IMG_UINT32 i;
	IMG_UINT32 index = -1;
	for (i = 0; i < psDeviceData->uiTimingDataSize; i++)
	{
		if (psDeviceData->pasTimingData[i].ui32HDisplay == width &&
		   psDeviceData->pasTimingData[i].ui32VDisplay == height)
		{
			index = i;
		}
	}

	return index;
}

static void FlipConfigQueueProcess(DCPDP_FLIP_CONTEXT *psContext, IMG_BOOL bFlipped, IMG_BOOL bFlush)
{
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	DCPDP_FLIP_CONFIG *psFlipConfig;
	PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib;
	IMG_HANDLE ahRetiredConfigs[DCPDP_MAX_COMMANDS_INFLIGHT] = {0};
	IMG_UINT32 i, ui32RetireConfigIndex = 0;
	unsigned long ulSpinlockFlags;

	DC_OSMutexLock(psContext->pvMutex);
	DC_OSSpinLockIRQSave(&psContext->sSpinLock, ulSpinlockFlags);

	psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
	pasSurfAttrib = psFlipConfig->pasSurfAttrib;

	do
	{
		switch (psFlipConfig->eStatus)
		{
			case DCPDP_FLIP_CONFIG_PENDING:
				if (!bFlipped)
				{
					Flip(psFlipConfig->psBuffer);

					bFlipped = IMG_TRUE;
				}

				/* If required, set the mode after flipping */
				if (pasSurfAttrib != NULL)
				{
					if (pasSurfAttrib[0].sDisplay.sDims.ui32Width != psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay ||
					    pasSurfAttrib[0].sDisplay.sDims.ui32Height != psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay)
					{
						psDeviceData->uiTimingDataIndex = GetTimingIndex(psDeviceData,
												 pasSurfAttrib[0].sDisplay.sDims.ui32Width,
												 pasSurfAttrib[0].sDisplay.sDims.ui32Height);
						SetModeRegisters(psDeviceData);	
						DCPDPEnableMemoryRequest(psDeviceData, IMG_TRUE);
					}
				}

				psFlipConfig->eStatus = DCPDP_FLIP_CONFIG_ACTIVE;

				/* Now that a new flip config is active we can retire the previous one (if one exists) */
				if (psContext->hConfigDataToRetire != NULL)
				{
					ahRetiredConfigs[ui32RetireConfigIndex++] = psContext->hConfigDataToRetire;
					psContext->hConfigDataToRetire = NULL;
				}

				/* Fall through */
			case DCPDP_FLIP_CONFIG_ACTIVE:
				/* We can retire this config only when it's buffer has been
				   displayed for at least the same number of frames as the
				   display period or if we're flushing the queue */
				if (psFlipConfig->ui32DisplayPeriod != 0)
				{
					psFlipConfig->ui32DisplayPeriod--;
				}

				if (psFlipConfig->ui32DisplayPeriod == 0 || bFlush)
				{
					DCPDP_FLIP_CONFIG *psNextFlipConfig;

					/* There should never be an outstanding config to retire at this point */
					DC_ASSERT(psContext->hConfigDataToRetire == NULL);

					/* Now that this config has been active for the minimum number of frames, i.e. the
					   display period, we can mark the flip config queue item as inactive. However, we
					   can only retire the config once a new config has been made active. Therefore,
					   store the Services config data handle so that we can retire it later. */
					psContext->hConfigDataToRetire = psFlipConfig->hConfigData;

					/* Reset the flip config data */
					psFlipConfig->hConfigData	= 0;
					psFlipConfig->psBuffer		= NULL;
					psFlipConfig->ui32DisplayPeriod	= 0;
					psFlipConfig->eStatus		= DCPDP_FLIP_CONFIG_INACTIVE;

					/* Move on the queue remove index */
					FlipConfigQueueAdvanceIndex(psContext, &psContext->ui32FlipConfigRemoveIndex);

					psNextFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];
					if (psNextFlipConfig->ui32DisplayPeriod == 0 || bFlush)
					{
						psFlipConfig = psNextFlipConfig;
						bFlipped = IMG_FALSE;
					}
				}
				break;
			case DCPDP_FLIP_CONFIG_INACTIVE:
				if (FlipConfigQueueEmptyNoLock(psContext))
				{
					break;
				}
				DC_OSDebugPrintf(DBGLVL_ERROR, " - Tried to process an inactive config when one wasn't expected\n");
			default:
				DC_ASSERT(0);
		}
	} while (psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_PENDING);

	DC_OSSpinUnlockIRQRestore(&psContext->sSpinLock, ulSpinlockFlags);

	/* Call into Services to retire any configs that were ready to be
	 * retired. */
	for (i = 0; i < ui32RetireConfigIndex; i++)
	{
		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(ahRetiredConfigs[i]);
	}

	DC_OSMutexUnlock(psContext->pvMutex);
}

static PVRSRV_ERROR VSyncProcessor(void *pvData)
{
	DCPDP_DEVICE *psDeviceData = pvData;

	psDeviceData->sPVRServicesFuncs.pfnCheckStatus(NULL);

	return PVRSRV_OK;
}

static PVRSRV_ERROR FlipConfigProcessor(void *pvData)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)pvData;

	DC_ASSERT(psContext);

	FlipConfigQueueProcess(psContext, IMG_TRUE, IMG_FALSE);

	return PVRSRV_OK;
}

static void DCPDPGetInfo(IMG_HANDLE hDeviceData, DC_DISPLAY_INFO *psDisplayInfo)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/* Copy our device name */
	DC_OSStringNCopy(psDisplayInfo->szDisplayName, DRVNAME " 1", DC_NAME_SIZE);

	/* Report what our minimum and maximum display period is. */
	psDisplayInfo->ui32MinDisplayPeriod	= DCPDP_MIN_DISPLAY_PERIOD;
	psDisplayInfo->ui32MaxDisplayPeriod	= DCPDP_MAX_DISPLAY_PERIOD;
	psDisplayInfo->ui32MaxPipes		= 1;
	psDisplayInfo->bUnlatchedSupported	= IMG_FALSE;
}

static PVRSRV_ERROR DCPDPPanelQueryCount(IMG_HANDLE hDeviceData, IMG_UINT32 *pui32NumPanels)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/* Don't support hot plug or multiple displays so hard code the value */
	*pui32NumPanels = 1;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPPanelQuery(IMG_HANDLE hDeviceData,
				    IMG_UINT32 ui32PanelsArraySize,
				    IMG_UINT32 *pui32NumPanels,
				    PVRSRV_PANEL_INFO *psPanelInfo)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

    PDP_CHECKPOINT;

	/* Don't support hot plug or multiple displays so hard code the value */
	*pui32NumPanels = 1;

	DC_OSMemSet(psPanelInfo, 0, sizeof *psPanelInfo);

	psPanelInfo[0].sSurfaceInfo.sFormat.ePixFormat = psDeviceData->ePixelFormat;
	psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_STRIDED;
	psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_NONE;

	psPanelInfo[0].sSurfaceInfo.sDims.ui32Width = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Height = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay;

	psPanelInfo[0].ui32RefreshRate = psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VRefresh;
	psPanelInfo[0].ui32XDpi = DCPDP_DPI;
	psPanelInfo[0].ui32YDpi = DCPDP_DPI;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPFormatQuery(IMG_HANDLE hDeviceData,
				     IMG_UINT32 ui32NumFormats,
				     PVRSRV_SURFACE_FORMAT *pasFormat,
				     IMG_UINT32 *pui32Supported)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	IMG_UINT32 i;

    PDP_CHECKPOINT;

	for (i = 0; i < ui32NumFormats; i++)
	{
		pui32Supported[i] = 0;

		if (pasFormat[i].ePixFormat == psDeviceData->ePixelFormat)
		{
			pui32Supported[i]++;
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPDimQuery(IMG_HANDLE hDeviceData,
				  IMG_UINT32 ui32NumDims,
				  PVRSRV_SURFACE_DIMS *psDim,
				  IMG_UINT32 *pui32Supported)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	IMG_UINT32 i;
	IMG_UINT32 j;

    PDP_CHECKPOINT;

	for (i = 0; i < ui32NumDims; i++)
	{
		pui32Supported[i] = 0;

		for (j = 0; j < psDeviceData->uiTimingDataSize; j++)
		{
			if ((psDim[i].ui32Width == psDeviceData->pasTimingData[j].ui32HDisplay) &&
		    	(psDim[i].ui32Height == psDeviceData->pasTimingData[j].ui32VDisplay))
			{
				pui32Supported[i]++;
			}
		}	
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPSetBlank(IMG_HANDLE hDeviceData,
								  IMG_BOOL bEnabled)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	IMG_UINT32 ui32Value;

    PDP_CHECKPOINT;

	ui32Value = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET);

	SET_FIELD(ui32Value, PDP_SYNCCTRL_POWERDN, bEnabled ? 1 : 0);
	psDeviceData->bVSyncEnabled = !bEnabled;

	DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_SYNCCTRL_OFFSET, ui32Value);

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPSetVSyncReporting(IMG_HANDLE hDeviceData,
										   IMG_BOOL bEnabled)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	psDeviceData->bVSyncReporting = bEnabled;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPLastVSyncQuery(IMG_HANDLE hDeviceData,
										IMG_INT64 *pi64Timestamp)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

	*pi64Timestamp = psDeviceData->i64LastVSyncTimeStamp;

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPContextCreate(IMG_HANDLE hDeviceData,
				       IMG_HANDLE *hDisplayContext)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;
	DCPDP_FLIP_CONTEXT *psContext;
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

    PDP_CHECKPOINT;

	if (psDeviceData->psFlipContext)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Cannot create a context when one is already active\n");
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	psContext = DC_OSCallocMem(sizeof *psContext);
	if (psContext == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Not enough memory to allocate a display context\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* We don't want to process the flip config work queue in interrupt context so
	   create a work queue which we can use to defer the work */
	eError = DC_OSWorkQueueCreate(&psContext->hFlipConfigWorkQueue, DCPDP_MAX_COMMANDS_INFLIGHT);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Couldn't create a work queue (%s)\n",
				 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPDPOSFreeMemContext;
	}

	/* Create work items to place on the flip config work queue when we service a vsync interrupt */
	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		eError = DC_OSWorkQueueCreateWorkItem(&psContext->asFlipConfigQueue[i].hWorkItem, FlipConfigProcessor, psContext);
		if (eError != PVRSRV_OK)
		{
			DC_OSDebugPrintf(DBGLVL_ERROR, " - Failed to create a flip config work item (%s)\n",
					 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
			goto ErrorDC_OSWorkQueueDestroyWorkItems;
		}
	}

	eError = DC_OSMutexCreate(&psContext->pvMutex);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Failed to create flip config mutex (%s)\n",
				 psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDC_OSWorkQueueDestroyWorkItems;
	}

	DC_OSSpinLockCreate(&psContext->sSpinLock);

	psContext->psDeviceData		= psDeviceData;
	psDeviceData->psFlipContext	= psContext;
	*hDisplayContext		= (IMG_HANDLE)psContext;

	return PVRSRV_OK;

ErrorDC_OSWorkQueueDestroyWorkItems:
	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		if (psContext->asFlipConfigQueue[i].hWorkItem != (IMG_HANDLE)NULL)
		{
			DC_OSWorkQueueDestroyWorkItem(psContext->asFlipConfigQueue[i].hWorkItem);
		}
	}

	DC_OSWorkQueueDestroy(psContext->hFlipConfigWorkQueue);

ErrorPDPOSFreeMemContext:
	DC_OSFreeMem(psContext);

	return eError;
}

static void DCPDPContextDestroy(IMG_HANDLE hDisplayContext)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	IMG_UINT32 i;

    PDP_CHECKPOINT;

	/* Make sure there are no work items on the queue before it's destroyed */
	DC_OSWorkQueueFlush(psContext->hFlipConfigWorkQueue);

	for (i = 0; i < DCPDP_MAX_COMMANDS_INFLIGHT; i++)
	{
		if (psContext->asFlipConfigQueue[i].hWorkItem != (IMG_HANDLE)NULL)
		{
			DC_OSWorkQueueDestroyWorkItem(psContext->asFlipConfigQueue[i].hWorkItem);
		}
	}

	DC_OSWorkQueueDestroy(psContext->hFlipConfigWorkQueue);
	DC_OSMutexDestroy(psContext->pvMutex);
	DC_OSSpinLockDestroy(&psContext->sSpinLock);

	psDeviceData->psFlipContext = NULL;
	DC_OSFreeMem(psContext);

}

static void DCPDPContextConfigure(IMG_HANDLE hDisplayContext,
				  IMG_UINT32 ui32PipeCount,
				  PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
				  IMG_HANDLE *ahBuffers,
				  IMG_UINT32 ui32DisplayPeriod,
				  IMG_HANDLE hConfigData)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	IMG_BOOL bQueueWasEmpty; 
    
	if (ui32DisplayPeriod > DCPDP_MAX_DISPLAY_PERIOD)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - Requested display period of %u is larger than maximum supported display period of %u (clamping)\n",
				 ui32DisplayPeriod, DCPDP_MAX_DISPLAY_PERIOD);
		DC_ASSERT(ui32DisplayPeriod <= DCPDP_MAX_DISPLAY_PERIOD);

		/* Clamp to something sane so that we can continue */
		ui32DisplayPeriod = DCPDP_MAX_DISPLAY_PERIOD;
	}

	/* Add new config to the queue */
	if (ui32PipeCount != 0)
	{
		/* Check if the queue is empty before adding a new flip config */
		bQueueWasEmpty = FlipConfigQueueEmpty(psContext);

		/* We have a regular config so add it as normal */
		FlipConfigQueueAdd(psContext, hConfigData, (DCPDP_BUFFER *)ahBuffers[0], pasSurfAttrib, ui32DisplayPeriod);

		/* Check to see if vsync unlocked flipping should be done. This is determined by the
		   display period being 0 and the flip config queue being empty before we added the
		   new flip config. If it wasn't empty then we need to allow the remaining vsync locked
		   flip configs to be processed before we can start processing the queue here. 
		   Also immediately flip if the current vsync interrupt is disabled.
		   This makes sure that even when the PDP is turned off we progress. */
		if (   (ui32DisplayPeriod == 0 && bQueueWasEmpty)
			|| psDeviceData->bVSyncEnabled == IMG_FALSE)
		{
			FlipConfigQueueProcess(psContext, IMG_FALSE, IMG_FALSE);
		}
	}
	else
	{
		/* We have a 'NULL' config, which indicates the display context is
		   about to be destroyed. Queue a flip back to the system buffer */
		FlipConfigQueueAdd(psContext, hConfigData, psDeviceData->psSystemBuffer, pasSurfAttrib, 0);

		/* Flush the remaining configs (this will result in the system buffer being displayed) */
		FlipConfigQueueProcess(psContext, IMG_FALSE, IMG_TRUE);

		DC_OSMutexLock(psContext->pvMutex);

		/* Immediately retire the system buffer config data */
		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(hConfigData);
		psContext->hConfigDataToRetire = NULL;

		DC_OSMutexUnlock(psContext->pvMutex);
	}            
}

static PVRSRV_ERROR DCPDPContextConfigureCheck(IMG_HANDLE hDisplayContext,
					       IMG_UINT32 ui32PipeCount,
					       PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
					       IMG_HANDLE *ahBuffers)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	DCPDP_BUFFER *psBuffer;
	IMG_UINT32 ui32NewVESATimingIndex; 
	ui32NewVESATimingIndex = GetTimingIndex(psDeviceData,pasSurfAttrib[0].sDisplay.sDims.ui32Width, pasSurfAttrib[0].sDisplay.sDims.ui32Height);

	if (ui32NewVESATimingIndex == -1)
	{
		return PVRSRV_ERROR_DC_INVALID_CONFIG;
	}

	if (ui32PipeCount != 1)
	{
		return PVRSRV_ERROR_DC_INVALID_CONFIG;
	}
	/* new crop dimensions, new mode dimensions */
	else if (pasSurfAttrib[0].sCrop.sDims.ui32Width != psDeviceData->pasTimingData[ui32NewVESATimingIndex].ui32HDisplay ||
		 pasSurfAttrib[0].sCrop.sDims.ui32Height != psDeviceData->pasTimingData[ui32NewVESATimingIndex].ui32VDisplay ||
		 pasSurfAttrib[0].sCrop.i32XOffset != 0 ||
		 pasSurfAttrib[0].sCrop.i32YOffset != 0)
	{
		return PVRSRV_ERROR_DC_INVALID_CROP_RECT;
	}
	/* new mode dimensions, new crop dimensions */
	else if (pasSurfAttrib[0].sDisplay.sDims.ui32Width != pasSurfAttrib[0].sCrop.sDims.ui32Width ||
		 pasSurfAttrib[0].sDisplay.sDims.ui32Height != pasSurfAttrib[0].sCrop.sDims.ui32Height ||
		 pasSurfAttrib[0].sDisplay.i32XOffset != pasSurfAttrib[0].sCrop.i32XOffset ||
		 pasSurfAttrib[0].sDisplay.i32YOffset != pasSurfAttrib[0].sCrop.i32YOffset)
	{
		return PVRSRV_ERROR_DC_INVALID_DISPLAY_RECT;
	}

	psBuffer = ahBuffers[0];
	
	/* new buffer dimensions, current mode dimensions */
	if (psBuffer->ui32Width <  pasSurfAttrib[0].sDisplay.sDims.ui32Width ||
	    psBuffer->ui32Height < pasSurfAttrib[0].sDisplay.sDims.ui32Height)
	{
		return PVRSRV_ERROR_DC_INVALID_BUFFER_DIMS;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPBufferSystemAcquire(IMG_HANDLE hDeviceData,
					     IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
					     IMG_UINT32 *pui32PageCount,
					     IMG_UINT32 *pui32PhysHeapID,
					     IMG_UINT32 *pui32ByteStride,
					     IMG_HANDLE *phSystemBuffer)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)hDeviceData;

    PDP_CHECKPOINT;

	OSAtomicIncrement(&psDeviceData->psSystemBuffer->i32RefCount);

	*puiLog2PageSize	= DC_OSGetPageShift();
	*pui32PageCount		= psDeviceData->psSystemBuffer->ui32SizeInPages;
	*pui32PhysHeapID	= psDeviceData->ui32PhysHeapID;
	*pui32ByteStride	= psDeviceData->psSystemBuffer->ui32ByteStride;
	*phSystemBuffer		= psDeviceData->psSystemBuffer;

	return PVRSRV_OK;
}

static void DCPDPBufferSystemRelease(IMG_HANDLE hSystemBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hSystemBuffer;

    PDP_CHECKPOINT;
    
	OSAtomicDecrement(&psBuffer->i32RefCount);
}

static PVRSRV_ERROR DCPDPBufferAlloc(IMG_HANDLE hDisplayContext,
				     DC_BUFFER_CREATE_INFO *psCreateInfo,
				     IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
				     IMG_UINT32 *pui32PageCount,
				     IMG_UINT32 *pui32PhysHeapID,
				     IMG_UINT32 *pui32ByteStride,
				     IMG_HANDLE *phBuffer)
{
	DCPDP_FLIP_CONTEXT *psContext = (DCPDP_FLIP_CONTEXT *)hDisplayContext;
	DCPDP_DEVICE *psDeviceData = psContext->psDeviceData;
	PVRSRV_SURFACE_INFO *psSurfInfo = &psCreateInfo->sSurface;
	DCPDP_BUFFER *psBuffer;

    PDP_CHECKPOINT;

	/* XXX: This need to change because we support more formats */
	DC_ASSERT(psCreateInfo->ui32BPP == DCPDP_PIXEL_FORMAT_BPP);

	if (psSurfInfo->sFormat.eMemLayout != PVRSRV_SURFACE_MEMLAYOUT_STRIDED)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	switch (psSurfInfo->sFormat.ePixFormat)
	{
		case DCPDP_PIXEL_FORMAT: // hard coded one supported format
			break;
		default:
			return PVRSRV_ERROR_UNSUPPORTED_PIXEL_FORMAT;
	}

	if (psSurfInfo->sFormat.ePixFormat != psDeviceData->ePixelFormat)
	{
		return PVRSRV_ERROR_UNSUPPORTED_PIXEL_FORMAT;
	}

	psBuffer = DC_OSAllocMem(sizeof *psBuffer);
	if (psBuffer == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSAtomicWrite(&psBuffer->i32RefCount, 0);
	psBuffer->psDeviceData		= psDeviceData;
	psBuffer->ePixelFormat		= psSurfInfo->sFormat.ePixFormat;
	psBuffer->ui32Width			= psSurfInfo->sDims.ui32Width;
	psBuffer->ui32Height		= psSurfInfo->sDims.ui32Height;
	psBuffer->ui32ByteStride	= psBuffer->ui32Width * psCreateInfo->ui32BPP;
	psBuffer->ui32SizeInBytes	= psDeviceData->ui32BufferSize;
	psBuffer->ui32SizeInPages	= psBuffer->ui32SizeInBytes >> DC_OSGetPageShift();

	if (BufferAcquireMemory(psDeviceData, psBuffer) == IMG_FALSE)
	{
		DC_OSFreeMem(psBuffer);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Make sure we get a buffer with the size that we're expecting (this should be page aligned) */
	DC_ASSERT((IMG_UINT32)DC_ALIGN(psBuffer->ui32ByteStride * psBuffer->ui32Height, DC_OSGetPageSize()) <= psDeviceData->ui32BufferSize);

	*puiLog2PageSize	= DC_OSGetPageShift();
	*pui32PageCount		= psBuffer->ui32SizeInPages;
	*pui32PhysHeapID	= psDeviceData->ui32PhysHeapID;
	*pui32ByteStride	= psBuffer->ui32ByteStride;
	*phBuffer		= psBuffer;
    
    PDP_DEBUG_PRINT("- %s: Acquired buffer at address %lx\n", __FUNCTION__, psBuffer->sCpuPAddr.uiAddr);
        
#if defined(PDP_DEBUG)
	{
		static int i = 0;
		gpsBuffer[i++] = psBuffer;
	}
#endif

	return PVRSRV_OK;
}

static PVRSRV_ERROR DCPDPBufferAcquire(IMG_HANDLE hBuffer, IMG_DEV_PHYADDR *pasDevPAddr, void **ppvLinAddr)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;
	DCPDP_DEVICE *psDeviceData = psBuffer->psDeviceData;
	IMG_CPU_PHYADDR sCpuPAddr = psBuffer->sCpuPAddr;
	IMG_UINT32 i;

    PDP_CHECKPOINT;

	for (i = 0; i < psBuffer->ui32SizeInPages; i++)
	{
		psDeviceData->sPVRServicesFuncs.pfnPhysHeapCpuPAddrToDevPAddr(psDeviceData->psPhysHeap, 1,
									      &pasDevPAddr[i], &sCpuPAddr);
		sCpuPAddr.uiAddr += DC_OSGetPageSize();
	}

	OSAtomicIncrement(&psBuffer->i32RefCount);

	*ppvLinAddr = NULL;

	return PVRSRV_OK;
}

static void DCPDPBufferRelease(IMG_HANDLE hBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;

	OSAtomicDecrement(&psBuffer->i32RefCount);
}

static void DCPDPBufferFree(IMG_HANDLE hBuffer)
{
	DCPDP_BUFFER *psBuffer = (DCPDP_BUFFER *)hBuffer;

    PDP_CHECKPOINT;

#if defined(DEBUG)
	if (OSAtomicRead(&psBuffer->i32RefCount) != 0)
	{
		DC_OSDebugPrintf(DBGLVL_WARNING, " - Freeing buffer that still has %d references\n", OSAtomicRead(&psBuffer->i32RefCount));
	}
#endif

	BufferReleaseMemory(psBuffer->psDeviceData, psBuffer);

	DC_OSFreeMem(psBuffer);
}

static DC_DEVICE_FUNCTIONS g_sDCFunctions =
{
	DCPDPGetInfo,				/* pfnGetInfo */
	DCPDPPanelQueryCount,		/* pfnPanelQueryCount */
	DCPDPPanelQuery,			/* pfnPanelQuery */
	DCPDPFormatQuery,			/* pfnFormatQuery */
	DCPDPDimQuery,				/* pfnDimQuery */
	DCPDPSetBlank,				/* pfnSetBlank */
	DCPDPSetVSyncReporting,		/* pfnSetVSyncReporting */
	DCPDPLastVSyncQuery,		/* pfnLastVSyncQuery */
	DCPDPContextCreate,			/* pfnContextCreate */
	DCPDPContextDestroy,		/* pfnContextDestroy */
	DCPDPContextConfigure,		/* pfnContextConfigure */
	DCPDPContextConfigureCheck,	/* pfnContextConfigureCheck */
	DCPDPBufferAlloc,			/* pfnBufferAlloc */
	DCPDPBufferAcquire,			/* pfnBufferAcquire */
	DCPDPBufferRelease,			/* pfnBufferRelease */
	DCPDPBufferFree,			/* pfnBufferFree */
	NULL,					/* pfnBufferImport */
	NULL,					/* pfnBufferMap */
	NULL,					/* pfnBufferUnmap */
	DCPDPBufferSystemAcquire,	/* pfnBufferSystemAcquire */
	DCPDPBufferSystemRelease,	/* pfnBufferSystemRelease */
};

static IMG_BOOL DisplayLISRHandler(void *pvData)
{
	DCPDP_DEVICE *psDeviceData = (DCPDP_DEVICE *)pvData;
	IMG_UINT32 ui32InterruptStatus;
	PVRSRV_ERROR eError;

	ui32InterruptStatus = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTSTAT_OFFSET);
	if (ui32InterruptStatus & PDP_INTSTAT_INTS_VBLNK0_MASK)
	{      
	    /* If a previous register update has completed, clear the status bit */
        if (DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_REGISTER_UPDATE_STATUS_OFFSET) & PDP_REGISTER_UPDATE_STATUS_REGISTERS_UPDATED_MASK)
        {
    	    DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_REGISTER_UPDATE_CTRL_OFFSET, 0);
        }
        
		/* Get the timestamp for this interrupt. */
		psDeviceData->i64LastVSyncTimeStamp = DC_OSClockns();
		if (psDeviceData->bVSyncReporting)
		{
			eError = DC_OSWorkQueueAddWorkItem(psDeviceData->hVSyncWorkQueue, psDeviceData->hVSyncWorkItem);
			if (eError != PVRSRV_OK)
			{
				DC_OSDebugPrintf(DBGLVL_WARNING,
						 " - %s: Couldn't queue work item (%s)\n",
						 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));

			}
		}

		if (psDeviceData->psFlipContext)
		{
			DCPDP_FLIP_CONTEXT *psContext = psDeviceData->psFlipContext;
			DCPDP_FLIP_CONFIG *psFlipConfig;

			DC_OSSpinLock(&psContext->sSpinLock);

			psFlipConfig = &psContext->asFlipConfigQueue[psContext->ui32FlipConfigRemoveIndex];

			if (psFlipConfig->eStatus == DCPDP_FLIP_CONFIG_PENDING)
			{
				Flip(psFlipConfig->psBuffer);
			}

			if (psFlipConfig->eStatus != DCPDP_FLIP_CONFIG_INACTIVE)
			{
				eError = DC_OSWorkQueueAddWorkItem(psContext->hFlipConfigWorkQueue, psFlipConfig->hWorkItem);
				if (eError != PVRSRV_OK)
				{
					DC_OSDebugPrintf(DBGLVL_WARNING,
							 " - %s: Couldn't queue work item (%s)\n",
							 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
				}
			}

			DC_OSSpinUnlock(&psContext->sSpinLock);
		}
		/* Clear the vsync status bit to show that the vsync interrupt has been handled */
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTCLR_OFFSET, (0x1UL << PDP_INTSTAT_INTS_VBLNK0_SHIFT));
        /* For some reason, first interrupt always contains HBLNK, so clear all */
        DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_INTCLR_OFFSET, 0xFFFFFFFF);
        
		return IMG_TRUE;
	}

	return IMG_FALSE;
}

static void DeInitSystemBuffer(DCPDP_DEVICE *psDeviceData)
{
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	if (PVRSRVGetDriverStatus() != PVRSRV_SERVICES_STATE_OK)
	{
				if (OSAtomicRead(&psDeviceData->psSystemBuffer->i32RefCount) != 0)
		{
			printk(KERN_INFO "%s: References to psSystemBuffer still exist"
				   " (%d)", __func__, OSAtomicRead(&psDeviceData->psSystemBuffer->i32RefCount));
		}
		OSAtomicWrite(&psDeviceData->psSystemBuffer->i32RefCount, 0);
	}
	else
#else
	{
		DC_ASSERT(OSAtomicRead(&psDeviceData->psSystemBuffer->i32RefCount) == 0);
	}
#endif

    PDP_CHECKPOINT;

    if (psDeviceData->psSystemBuffer != NULL)
    {
		BufferReleaseMemory(psDeviceData, psDeviceData->psSystemBuffer);

		DC_OSFreeMem(psDeviceData->psSystemBuffer);
		psDeviceData->psSystemBuffer = NULL;
	}
}

static PVRSRV_ERROR InitSystemBuffer(DCPDP_DEVICE *psDeviceData)
{
	DCPDP_BUFFER *psBuffer;
	IMG_CPU_VIRTADDR pvBufferCpuVAddr;
	IMG_UINT32 *pui32Pixel;
	IMG_UINT8 ui8Red, ui8Green[4], ui8Blue;
	IMG_UINT32 i, j;
	IMG_UINT64 ui64BufferCount;

	/* On a reconfigure, system buffer will not be NULL */
	if (psDeviceData->psSystemBuffer != NULL)
	{
		DeInitSystemBuffer(psDeviceData);
		psDeviceData->psSystemBuffer = NULL;
	}

	/* Setup simple buffer allocator */
	psDeviceData->ui32BufferSize = (IMG_UINT32)DC_ALIGN(psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay *
							    psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay * 
							    DCPDP_PIXEL_FORMAT_BPP,
							    DC_OSGetPageSize());

	ui64BufferCount = psDeviceData->uiDispMemSize;
	ui64BufferCount = DC_OSDiv64(ui64BufferCount, psDeviceData->ui32BufferSize);

	psDeviceData->ui32BufferCount = (IMG_UINT32)ui64BufferCount;
	if (psDeviceData->ui32BufferCount == 0)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Not enough space for the framebuffer\n", __FUNCTION__);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	else if (psDeviceData->ui32BufferCount > DCPDP_MAX_BUFFERS)
	{
		psDeviceData->ui32BufferCount = DCPDP_MAX_BUFFERS;
	}

	psDeviceData->ui32BufferUseMask = 0;

	psBuffer = DC_OSAllocMem(sizeof *psBuffer);
	if (psBuffer == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSAtomicWrite(&psBuffer->i32RefCount, 0);
	psBuffer->psDeviceData		= psDeviceData;
	psBuffer->ePixelFormat		= psDeviceData->ePixelFormat;
	psBuffer->ui32Width			= psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay;
	psBuffer->ui32Height		= psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay;
	psBuffer->ui32ByteStride	= psBuffer->ui32Width * DCPDP_PIXEL_FORMAT_BPP;
	psBuffer->ui32SizeInBytes	= psDeviceData->ui32BufferSize;
	psBuffer->ui32SizeInPages	= psBuffer->ui32SizeInBytes >> DC_OSGetPageShift();

	if (BufferAcquireMemory(psDeviceData, psBuffer) == IMG_FALSE)
	{
		DC_OSFreeMem(psBuffer);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

    PDP_DEBUG_PRINT("- %s: System buffer width: %d, height: %d, starting addr: %lx\n", __FUNCTION__, psBuffer->ui32Width, psBuffer->ui32Height, psBuffer->sCpuPAddr.uiAddr);

	/* Make sure we get a buffer with the size that we're expecting (this should be page aligned) */
	DC_ASSERT((IMG_UINT32)DC_ALIGN(psBuffer->ui32ByteStride * psBuffer->ui32Height, DC_OSGetPageSize()) <= psDeviceData->ui32BufferSize);

	/* Initialise the system buffer to a nice rainbow. */
	pvBufferCpuVAddr = DC_OSMapPhysAddr(psBuffer->sCpuPAddr, psBuffer->ui32SizeInBytes);
	if (pvBufferCpuVAddr != NULL)
	{
		pui32Pixel = (IMG_UINT32 *)pvBufferCpuVAddr;

#if (DCPDP_PIXEL_FORMAT_BPP == 4)
		for (i = 0; i < psBuffer->ui32Height; ++i)
		{
			for (j = 0; j < psBuffer->ui32Width; ++j)
			{
				IMG_UINT32 index = i * psBuffer->ui32Width + j;
				ui8Red   = 0xFF - (((i + 1) * 0xFF) / psBuffer->ui32Height);
				ui8Green[0] = ((j + 1) * 0xFF) / psBuffer->ui32Width;
				ui8Blue  = ((i + 1) * 0xFF) / psBuffer->ui32Height;
				pui32Pixel[index] = 0xFF000000 | (ui8Red << 16) | (ui8Green[0] << 8) | (ui8Blue << 0);
			}
		}
#endif

#if (DCPDP_PIXEL_FORMAT_BPP == 3)
		/* Optimization for faster upload on emu, i varies over the height, j the width, and k the pixel buffer */
		/* Relies on the width being a factor of 4 */
		for (i = 0; i < psBuffer->ui32Height; ++i)
		{
			for (j = k = 0; j < psBuffer->ui32Width; j += 4, k += 3)
			{
				index = i * ((psBuffer->ui32Width * 3) / 4) + k;
				ui8Red      = 0xFF - (((i + 1) * 0xFF) / psBuffer->ui32Height);
				ui8Green[0] = ((j + 1) * 0xFF) / psBuffer->ui32Width;
				ui8Green[1] = ((j + 2) * 0xFF) / psBuffer->ui32Width;
				ui8Green[2] = ((j + 3) * 0xFF) / psBuffer->ui32Width;
				ui8Green[3] = ((j + 4) * 0xFF) / psBuffer->ui32Width;
				ui8Blue     = ((i + 1) * 0xFF) / psBuffer->ui32Height;
				pui32Pixel[index + 0] = (ui8Blue << 24)     | (ui8Red << 16)      | (ui8Green[0] << 8) | (ui8Blue);
				pui32Pixel[index + 1] = (ui8Green[1] << 24) | (ui8Blue << 16)     | (ui8Red << 8)      | (ui8Green[2]); 
				pui32Pixel[index + 2] = (ui8Red << 24)      | (ui8Green[3] << 16) | (ui8Blue << 8)     | (ui8Red);
			}
		}
#endif
		DC_OSUnmapPhysAddr(pvBufferCpuVAddr, psBuffer->ui32SizeInBytes);
	}

	psDeviceData->psSystemBuffer = psBuffer;

	return PVRSRV_OK;
}


static PVRSRV_ERROR InitTimingData(DCPDP_DEVICE * pvDevice)
{
	const DCPDP_MODULE_PARAMETERS * pvModuleParameters;
	DCPDP_TIMING_DATA * psTimingData;

	pvDevice->uiTimingDataSize = 1;
	pvDevice->pasTimingData = DC_OSAllocMem(sizeof(DCPDP_TIMING_DATA) * pvDevice->uiTimingDataSize);
	if (pvDevice->pasTimingData	== NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	psTimingData = &pvDevice->pasTimingData[pvDevice->uiTimingDataIndex];

	pvModuleParameters = DCPDPGetModuleParameters();

	psTimingData->ui32HDisplay      = pvModuleParameters->ui32PDPWidth; 
	psTimingData->ui32HTotal        = psTimingData->ui32HDisplay; 
	psTimingData->ui32HBackPorch    = 0; 
	psTimingData->ui32HFrontPorch   = psTimingData->ui32HTotal; 
	psTimingData->ui32HLeftBorder   = psTimingData->ui32HBackPorch;
	psTimingData->ui32HActiveStart  = psTimingData->ui32HLeftBorder;
	psTimingData->ui32HRightBorder  = psTimingData->ui32HActiveStart + psTimingData->ui32HDisplay; 
	psTimingData->ui8HSyncPolarity  = 0;

	psTimingData->ui32VDisplay      = pvModuleParameters->ui32PDPHeight; 
	psTimingData->ui32VTotal        = psTimingData->ui32VDisplay;
	psTimingData->ui32VBackPorch    = 0;
	psTimingData->ui32VFrontPorch   = psTimingData->ui32VTotal; 
	psTimingData->ui32VTopBorder    = psTimingData->ui32VBackPorch;
	psTimingData->ui32VActiveStart  = psTimingData->ui32VTopBorder;
	psTimingData->ui32VBottomBorder = psTimingData->ui32VActiveStart + psTimingData->ui32VDisplay; 
	psTimingData->ui8VSyncPolarity  = 0;
	psTimingData->ui32VRefresh   = 60;

	PDP_DEBUG_PRINT("- %s: HDisplay: %d, HActive: %d, HLeftBorder: %d, HTotal: %d, HFrontPorch: %d, HBackPorch: %d, HRightBorder: %d\n", __FUNCTION__, psTimingData->ui32HDisplay, psTimingData->ui32HActiveStart,
	psTimingData->ui32HLeftBorder, psTimingData->ui32HTotal, psTimingData->ui32HFrontPorch, psTimingData->ui32HBackPorch, psTimingData->ui32HRightBorder);

	PDP_DEBUG_PRINT("- %s: VDisplay: %d, VActive: %d, VTopBorder: %d, VTotal: %d, VFrontPorch: %d, VBackPorch: %d, VBottomBorder: %d\n", __FUNCTION__, psTimingData->ui32VDisplay, psTimingData->ui32VActiveStart,
	psTimingData->ui32VTopBorder, psTimingData->ui32VTotal, psTimingData->ui32VFrontPorch, psTimingData->ui32VBackPorch, psTimingData->ui32VBottomBorder);

	return PVRSRV_OK;
}

static void UpdateTimingData(DCPDP_DEVICE * pvDevice)
{
	DCPDP_TIMING_DATA * psTimingData = &pvDevice->pasTimingData[pvDevice->uiTimingDataIndex];
	DTD * dtd;

	if (pvDevice->videoParams.mDtdActiveIndex > HDMI_DTD_MAX)
	{
		PDP_ERROR_PRINT(" - %s: DTD Active index is greater than max (%d), resetting to 0\n", __FUNCTION__, HDMI_DTD_MAX);
		pvDevice->videoParams.mDtdActiveIndex = 0;
	}

	dtd = &pvDevice->videoParams.mDtdList[pvDevice->videoParams.mDtdActiveIndex];

	dtd->mHBackPorchWidth = dtd->mHFrontPorchWidth;
	dtd->mVBackPorchWidth = dtd->mVFrontPorchWidth;

	psTimingData->ui32HDisplay      = dtd->mHActive; 
	psTimingData->ui32HTotal        = dtd->mHActive + dtd->mHBlanking + 2 * dtd->mHBorder; 
	psTimingData->ui32HBackPorch    = dtd->mHSyncPulseWidth; 
	psTimingData->ui32HFrontPorch   = psTimingData->ui32HTotal - dtd->mHFrontPorchWidth; 
	psTimingData->ui32HLeftBorder   = psTimingData->ui32HBackPorch + dtd->mHBackPorchWidth;
	psTimingData->ui32HActiveStart  = psTimingData->ui32HLeftBorder + dtd->mHBorder;
	psTimingData->ui32HRightBorder  = psTimingData->ui32HActiveStart + psTimingData->ui32HDisplay; 
	psTimingData->ui8HSyncPolarity  = !dtd->mHSyncPolarity; // For PDP, 0 means active high

	psTimingData->ui32VDisplay      = dtd->mVActive; 
	psTimingData->ui32VTotal        = dtd->mVActive + dtd->mVBlanking + 2 * dtd->mVBorder; 
	psTimingData->ui32VBackPorch    = dtd->mVSyncPulseWidth;
	psTimingData->ui32VFrontPorch   = psTimingData->ui32VTotal - dtd->mVFrontPorchWidth; 
	psTimingData->ui32VTopBorder    = psTimingData->ui32VBackPorch + dtd->mVBackPorchWidth; //dtd->mVBlanking - dtd->mVFrontPorchWidth;
	psTimingData->ui32VActiveStart  = psTimingData->ui32VTopBorder + dtd->mVBorder;
	psTimingData->ui32VBottomBorder = psTimingData->ui32VActiveStart + psTimingData->ui32VDisplay; 
	psTimingData->ui8VSyncPolarity  = !dtd->mVSyncPolarity; // For PDP, 0 means active high

	if (dtd->mHSyncPolarity == 1 && dtd->mVSyncPolarity == 0)
	{
		psTimingData->bReducedBlanking = IMG_TRUE;
	}

	PDP_DEBUG_PRINT("- %s: HDisplay: %d, HActive: %d, HLeftBorder: %d, HTotal: %d, HFrontPorch: %d, HBackPorch: %d, HRightBorder: %d\n", __FUNCTION__, psTimingData->ui32HDisplay, psTimingData->ui32HActiveStart,
	psTimingData->ui32HLeftBorder, psTimingData->ui32HTotal, psTimingData->ui32HFrontPorch, psTimingData->ui32HBackPorch, psTimingData->ui32HRightBorder);

	PDP_DEBUG_PRINT("- %s: VDisplay: %d, VActive: %d, VTopBorder: %d, VTotal: %d, VFrontPorch: %d, VBackPorch: %d, VBottomBorder: %d\n", __FUNCTION__, psTimingData->ui32VDisplay, psTimingData->ui32VActiveStart,
	psTimingData->ui32VTopBorder, psTimingData->ui32VTotal, psTimingData->ui32VFrontPorch, psTimingData->ui32VBackPorch, psTimingData->ui32VBottomBorder);

	psTimingData->ui32VRefresh   = 60;

}

PVRSRV_ERROR DCPDPInit(void *pvDevice, DCPDP_DEVICE **ppsDeviceData)
{
	DCPDP_DEVICE *psDeviceData;
	PVRSRV_ERROR eError;

	if (ppsDeviceData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	DC_OSSetDrvName(DRVNAME);

    PDP_CHECKPOINT;

	psDeviceData = DC_OSCallocMem(sizeof *psDeviceData);
	if (psDeviceData == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to allocate device data\n", __FUNCTION__);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psDeviceData->pvDevice = pvDevice;
	psDeviceData->ePixelFormat = DCPDP_PIXEL_FORMAT;
    #if defined(PDP_DEBUG)
    gPdpDevice = psDeviceData;
    #endif

	eError = DC_OSPVRServicesConnectionOpen(&psDeviceData->hPVRServicesConnection);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to open connection to PVR Services (%d)\n",
				 __FUNCTION__, eError);
		goto ErrorFreeDeviceData;
	}

	eError = DC_OSPVRServicesSetupFuncs(psDeviceData->hPVRServicesConnection, &psDeviceData->sPVRServicesFuncs);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to setup PVR Services function table (%d)\n",
				 __FUNCTION__, eError);
		goto ErrorPDPOSPVRServicesConnectionClose;
	}

	/* Services manages the card memory so we need to acquire the display controller
	   heap (which is a carve out of the card memory) so we can allocate our own buffers. */
	psDeviceData->ui32PhysHeapID = DCPDP_PHYS_HEAP_ID;

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapAcquire(psDeviceData->ui32PhysHeapID,
								    &psDeviceData->psPhysHeap);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to acquire heap (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPDPOSPVRServicesConnectionClose;
	}

	/* The PDP can only access local memory so make sure the heap is of the correct type */
	if (psDeviceData->sPVRServicesFuncs.pfnPhysHeapGetType(psDeviceData->psPhysHeap) != PHYS_HEAP_TYPE_LMA)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Heap is of the wrong type\n", __FUNCTION__);
		eError = PVRSRV_ERROR_INVALID_HEAP;
		goto ErrorPhysHeapRelease;
	}

	/* Get the CPU base address for the display heap */
	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapRegionGetCpuPAddr(psDeviceData->psPhysHeap,
										0,
								       &psDeviceData->sDispMemCpuPAddr);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to get display memory base address (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPhysHeapRelease;
	}

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapRegionGetSize(psDeviceData->psPhysHeap,
									0,
								    &psDeviceData->uiDispMemSize);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to get the display memory size (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorPhysHeapRelease;
	}

	DC_OSDebugPrintf(DBGLVL_DEBUG, " - %s: Display memory size (0x%016lX)\n",
			 __FUNCTION__, psDeviceData->uiDispMemSize);

	/* Reserve and map the PDP registers */
	psDeviceData->sPDPRegCpuPAddr.uiAddr =
		DC_OSAddrRangeStart(psDeviceData->pvDevice, DCPDP2_REG_PCI_BASENUM) +
		DCPDP2_PCI_PDP_REG_OFFSET;

	if (DC_OSRequestAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP2_PCI_PDP_REG_SIZE, DRVNAME) == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to reserve the PDP registers\n", __FUNCTION__);

		psDeviceData->sPDPRegCpuPAddr.uiAddr = 0;
		eError = PVRSRV_ERROR_PCI_REGION_UNAVAILABLE;

		goto ErrorPhysHeapRelease;
	}

	psDeviceData->pvPDPRegCpuVAddr = DC_OSMapPhysAddr(psDeviceData->sPDPRegCpuPAddr, DCPDP2_PCI_PDP_REG_SIZE);
	if (psDeviceData->pvPDPRegCpuVAddr == NULL)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to map PDP registers\n", __FUNCTION__);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;

		goto ErrorReleaseMemRegion;
	}

	{
		/*
		 * 1GB Plato boards have issue with PDP initialisation. When PDP fails to
		 * properly initialize, PDP registers are inaccessible.
		 *
		 * We can detect this by reading a register that has a known
		 * and unchanging value.
		 */
		#define PLATO_PDP_CORE_ID (0x07010003)

		IMG_UINT32 ui32CoreId;

		ui32CoreId =
			DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_CORE_ID_OFFSET);

		if (ui32CoreId == PLATO_PDP_CORE_ID)
		{

			DC_OSDebugPrintf(DBGLVL_INFO, "%s: PDP initialized! (CoreId = 0x%x)",
				   	__FUNCTION__, ui32CoreId);
		}
		else
		{
			DC_OSDebugPrintf(DBGLVL_INFO, "%s: PDP failed to initialize! (CoreId = 0x%x)"
					, __FUNCTION__, ui32CoreId);

			goto ErrorPDPOSUnmapPDPRegisters;
		}
	}

	psDeviceData->pvPDPBifRegCpuVAddr = psDeviceData->pvPDPRegCpuVAddr + DCPDP2_BIF_REGS_OFFSET;

    PDP_DEBUG_PRINT(" - %s: Register base CpuPAddr: %lx\n", __FUNCTION__, psDeviceData->sPDPRegCpuPAddr.uiAddr);
    PDP_DEBUG_PRINT(" - %s: Register base CpuVAddr: %p\n", __FUNCTION__, psDeviceData->pvPDPRegCpuVAddr);

	/* Initialize timing data to defaults */
	eError = InitTimingData(psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to initialize timing data\n", __FUNCTION__);
		goto ErrorPDPOSUnmapPDPRegisters;
	}

	/* Create a system buffer */
	eError = InitSystemBuffer(psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to initialise the system buffer (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorFreeTimingData;
	}

	eError = psDeviceData->sPVRServicesFuncs.pfnDCRegisterDevice(&g_sDCFunctions,
								     DCPDP_MAX_COMMANDS_INFLIGHT,
								     psDeviceData,
								     &psDeviceData->hPVRServicesDevice);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to register the display device (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDeinitSysBuffer;
	}

	/* Register our handler */
	eError = psDeviceData->sPVRServicesFuncs.pfnSysInstallDeviceLISR(pvDevice,
									 PLATO_IRQ_PDP,
									 DRVNAME,
									 DisplayLISRHandler,
									 psDeviceData,
									 &psDeviceData->hLISRData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to install the display device interrupt handler (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCUnregisterDevice;
	}

	eError = DC_OSWorkQueueCreate(&psDeviceData->hVSyncWorkQueue, 1);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to create a vsync work queue (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCUninstallLISR;
	}

	eError = DC_OSWorkQueueCreateWorkItem(&psDeviceData->hVSyncWorkItem, VSyncProcessor, psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to create a vsync work item (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		goto ErrorDCDestroyWorkQueue;
	}

	*ppsDeviceData = psDeviceData;

	return PVRSRV_OK;

ErrorDCDestroyWorkQueue:
	DC_OSWorkQueueDestroy(psDeviceData->hVSyncWorkQueue);

ErrorDCUninstallLISR:
	psDeviceData->sPVRServicesFuncs.pfnSysUninstallDeviceLISR(psDeviceData->hLISRData);

ErrorDCUnregisterDevice:
	psDeviceData->sPVRServicesFuncs.pfnDCUnregisterDevice(psDeviceData->hPVRServicesDevice);

ErrorDeinitSysBuffer:
	DeInitSystemBuffer(psDeviceData);

ErrorFreeTimingData:
	DC_OSFreeMem(psDeviceData->pasTimingData);

ErrorPDPOSUnmapPDPRegisters:
	DC_OSUnmapPhysAddr(psDeviceData->pvPDPRegCpuVAddr, DCPDP2_PCI_PDP_REG_SIZE);

ErrorReleaseMemRegion:
	DC_OSReleaseAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP2_PCI_PDP_REG_SIZE);

ErrorPhysHeapRelease:
	psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);

ErrorPDPOSPVRServicesConnectionClose:
	DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

ErrorFreeDeviceData:
	DC_OSFreeMem(psDeviceData);

	return eError;

}

PVRSRV_ERROR DCPDPStart(DCPDP_DEVICE *psDeviceData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	UpdateTimingData(psDeviceData);

	/* Create a system buffer */
	eError = InitSystemBuffer(psDeviceData);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to initialise the system buffer (%s)\n",
				 __FUNCTION__, psDeviceData->sPVRServicesFuncs.pfnGetErrorString(eError));
		return eError;
	}

	SetModeRegisters(psDeviceData);
	psDeviceData->bVSyncEnabled = IMG_TRUE;

	Flip(psDeviceData->psSystemBuffer);

    EnableVSyncInterrupt(psDeviceData);

	/* Print some useful information */
	DC_OSDebugPrintf(DBGLVL_INFO, " using mode %ux%u@%uHz (phys: %ux%u)\n",
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay,
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay,
			 psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VRefresh,
			 (IMG_UINT32)(((psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32HDisplay * 1000) / DCPDP_DPI * 254) / 10000),
			 (IMG_UINT32)(((psDeviceData->pasTimingData[psDeviceData->uiTimingDataIndex].ui32VDisplay * 1000) / DCPDP_DPI * 254) / 10000));
	DC_OSDebugPrintf(DBGLVL_INFO, " register base: 0x%llxK\n", psDeviceData->sPDPRegCpuPAddr.uiAddr);
	DC_OSDebugPrintf(DBGLVL_INFO, " memory base: 0x%llxK\n", psDeviceData->sDispMemCpuPAddr.uiAddr);

	return PVRSRV_OK;

}

void DCPDPDeInit(DCPDP_DEVICE *psDeviceData, void **ppvDevice)
{
	PDP_CHECKPOINT;

	if (psDeviceData)
	{
		DC_OSWorkQueueDestroyWorkItem(psDeviceData->hVSyncWorkItem);
		DC_OSWorkQueueDestroy(psDeviceData->hVSyncWorkQueue);

		DisableVSyncInterrupt(psDeviceData);

		psDeviceData->sPVRServicesFuncs.pfnDCUnregisterDevice(psDeviceData->hPVRServicesDevice);
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
		if (PVRSRVGetDriverStatus() != PVRSRV_SERVICES_STATE_OK)
		{
			psDeviceData->psFlipContext = NULL;
		}
		else
#else
		{
			DC_ASSERT(psDeviceData->psFlipContext == NULL);
		}
#endif
		(void)psDeviceData->sPVRServicesFuncs.pfnSysUninstallDeviceLISR(psDeviceData->hLISRData);

		DeInitSystemBuffer(psDeviceData);

		/* Disable stream 1 to ensure PDP2 doesn't try to access on card memory */
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1CTRL_OFFSET, PLACE_FIELD(PDP_GRPH1CTRL_GRPH1STREN, 0));

		DC_OSUnmapPhysAddr(psDeviceData->pvPDPRegCpuVAddr, DCPDP2_PCI_PDP_REG_SIZE);

		DC_OSReleaseAddrRegion(psDeviceData->sPDPRegCpuPAddr, DCPDP2_PCI_PDP_REG_SIZE);

		psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);

		DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

		if (ppvDevice != NULL)
		{
			*ppvDevice = psDeviceData->pvDevice;
		}

        PDP_CHECKPOINT;

		DC_OSFreeMem(psDeviceData->pasTimingData);
		DC_OSFreeMem(psDeviceData);
	}
}

void DCPDPEnableMemoryRequest(DCPDP_DEVICE *psDeviceData, IMG_BOOL bEnable)
{
	if (psDeviceData)
	{
		IMG_UINT32 ui32Value;
		ui32Value = DC_OSReadReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1CTRL_OFFSET);
		SET_FIELD(ui32Value, PDP_GRPH1CTRL_GRPH1STREN, bEnable ? 1 : 0);
		DC_OSWriteReg32(psDeviceData->pvPDPRegCpuVAddr, PDP_GRPH1CTRL_OFFSET, ui32Value);
	}
}

