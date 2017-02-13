/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxray
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxray
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

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxray.h"
#include "devicemem_server.h"


#include "common_rgxray_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>





/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateRPMFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERPMFREELIST *psRGXCreateRPMFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERPMFREELIST *psRGXCreateRPMFreeListOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RPM_CONTEXT * psRPMContextInt = NULL;
	RGX_RPM_FREELIST * psCleanupCookieInt = NULL;





	PMRLock();






				{
					/* Look up the address from the handle */
					psRGXCreateRPMFreeListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psRPMContextInt,
											psRGXCreateRPMFreeListIN->hRPMContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);
					if(psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXCreateRPMFreeList_exit;
					}
				}

	psRGXCreateRPMFreeListOUT->eError =
		RGXCreateRPMFreeList(psConnection, OSGetDevData(psConnection),
					psRPMContextInt,
					psRGXCreateRPMFreeListIN->ui32InitFLPages,
					psRGXCreateRPMFreeListIN->ui32GrowFLPages,
					psRGXCreateRPMFreeListIN->sFreeListDevVAddr,
					&psCleanupCookieInt,
					&psRGXCreateRPMFreeListOUT->ui32HWFreeList);
	/* Exit early if bridged call fails */
	if(psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto RGXCreateRPMFreeList_exit;
	}
	PMRUnlock();






	psRGXCreateRPMFreeListOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXCreateRPMFreeListOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&RGXDestroyRPMFreeList);
	if (psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMFreeList_exit;
	}




RGXCreateRPMFreeList_exit:
	if (psRGXCreateRPMFreeListOUT->eError != PVRSRV_OK)
	{
		if (psCleanupCookieInt)
		{
			RGXDestroyRPMFreeList(psCleanupCookieInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRPMFreeList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRPMFREELIST *psRGXDestroyRPMFreeListIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRPMFREELIST *psRGXDestroyRPMFreeListOUT,
					 CONNECTION_DATA *psConnection)
{





	PMRLock();








	psRGXDestroyRPMFreeListOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRPMFreeListIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST);
	if ((psRGXDestroyRPMFreeListOUT->eError != PVRSRV_OK) && (psRGXDestroyRPMFreeListOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		PMRUnlock();
		goto RGXDestroyRPMFreeList_exit;
	}


	PMRUnlock();


RGXDestroyRPMFreeList_exit:

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateRPMContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERPMCONTEXT *psRGXCreateRPMContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERPMCONTEXT *psRGXCreateRPMContextOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RPM_CONTEXT * psCleanupCookieInt = NULL;
	DEVMEMINT_HEAP * psSceneHeapInt = NULL;
	DEVMEMINT_HEAP * psRPMPageTableHeapInt = NULL;
	DEVMEM_MEMDESC * psHWMemDescInt = NULL;



	psRGXCreateRPMContextOUT->hCleanupCookie = NULL;








				{
					/* Look up the address from the handle */
					psRGXCreateRPMContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psSceneHeapInt,
											psRGXCreateRPMContextIN->hSceneHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateRPMContext_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psRGXCreateRPMContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psRPMPageTableHeapInt,
											psRGXCreateRPMContextIN->hRPMPageTableHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateRPMContext_exit;
					}
				}

	psRGXCreateRPMContextOUT->eError =
		RGXCreateRPMContext(psConnection, OSGetDevData(psConnection),
					&psCleanupCookieInt,
					psRGXCreateRPMContextIN->ui32TotalRPMPages,
					psRGXCreateRPMContextIN->ui32Log2DopplerPageSize,
					psRGXCreateRPMContextIN->sSceneMemoryBaseAddr,
					psRGXCreateRPMContextIN->sDopplerHeapBaseAddr,
					psSceneHeapInt,
					psRGXCreateRPMContextIN->sRPMPageTableBaseAddr,
					psRPMPageTableHeapInt,
					&psHWMemDescInt,
					&psRGXCreateRPMContextOUT->ui32HWFrameData);
	/* Exit early if bridged call fails */
	if(psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMContext_exit;
	}






	psRGXCreateRPMContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXCreateRPMContextOUT->hCleanupCookie,
							(void *) psCleanupCookieInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&RGXDestroyRPMContext);
	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMContext_exit;
	}






	psRGXCreateRPMContextOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,

							&psRGXCreateRPMContextOUT->hHWMemDesc,
							(void *) psHWMemDescInt,
							PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psRGXCreateRPMContextOUT->hCleanupCookie);
	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRPMContext_exit;
	}




RGXCreateRPMContext_exit:
	if (psRGXCreateRPMContextOUT->eError != PVRSRV_OK)
	{
		if (psRGXCreateRPMContextOUT->hCleanupCookie)
		{


			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psRGXCreateRPMContextOUT->hCleanupCookie,
						PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psCleanupCookieInt = NULL;
		}


		if (psCleanupCookieInt)
		{
			RGXDestroyRPMContext(psCleanupCookieInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRPMContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRPMCONTEXT *psRGXDestroyRPMContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRPMCONTEXT *psRGXDestroyRPMContextOUT,
					 CONNECTION_DATA *psConnection)
{













	psRGXDestroyRPMContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRPMContextIN->hCleanupCookie,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT);
	if ((psRGXDestroyRPMContextOUT->eError != PVRSRV_OK) && (psRGXDestroyRPMContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyRPMContext_exit;
	}




RGXDestroyRPMContext_exit:

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXKickRS(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKRS *psRGXKickRSIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKRS *psRGXKickRSOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = NULL;
	IMG_HANDLE *hServerSyncsInt2 = NULL;
	IMG_BYTE *psDMCmdInt = NULL;
	IMG_BYTE *psFCDMCmdInt = NULL;




	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientFenceUFOSyncPrimBlockInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
		hClientFenceUFOSyncPrimBlockInt2 = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE));
		if (!hClientFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->phClientFenceUFOSyncPrimBlock, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickRSIN->phClientFenceUFOSyncPrimBlock,
				psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceSyncOffsetInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceSyncOffsetInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->pui32ClientFenceSyncOffset, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceSyncOffsetInt, psRGXKickRSIN->pui32ClientFenceSyncOffset,
				psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceValueInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->pui32ClientFenceValue, psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickRSIN->pui32ClientFenceValue,
				psRGXKickRSIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
		hClientUpdateUFOSyncPrimBlockInt2 = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->phClientUpdateUFOSyncPrimBlock, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickRSIN->phClientUpdateUFOSyncPrimBlock,
				psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateSyncOffsetInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateSyncOffsetInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->pui32ClientUpdateSyncOffset, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateSyncOffsetInt, psRGXKickRSIN->pui32ClientUpdateSyncOffset,
				psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateValueInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->pui32ClientUpdateValue, psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickRSIN->pui32ClientUpdateValue,
				psRGXKickRSIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncFlagsInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->pui32ServerSyncFlags, psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickRSIN->pui32ServerSyncFlags,
				psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = OSAllocMemNoStats(psRGXKickRSIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncsInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
		hServerSyncsInt2 = OSAllocMemNoStats(psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncsInt2)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->phServerSyncs, psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickRSIN->phServerSyncs,
				psRGXKickRSIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32CmdSize != 0)
	{
		psDMCmdInt = OSAllocMemNoStats(psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE));
		if (!psDMCmdInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->psDMCmd, psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psDMCmdInt, psRGXKickRSIN->psDMCmd,
				psRGXKickRSIN->ui32CmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}
	if (psRGXKickRSIN->ui32FCCmdSize != 0)
	{
		psFCDMCmdInt = OSAllocMemNoStats(psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE));
		if (!psFCDMCmdInt)
		{
			psRGXKickRSOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickRS_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickRSIN->psFCDMCmd, psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psFCDMCmdInt, psRGXKickRSIN->psFCDMCmd,
				psRGXKickRSIN->ui32FCCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickRSOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickRS_exit;
			}

	PMRLock();






				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											psRGXKickRSIN->hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickRS_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickRS_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickRS_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickRSIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickRSOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickRSOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickRS_exit;
					}
				}
		}
	}

	psRGXKickRSOUT->eError =
		PVRSRVRGXKickRSKM(
					psRayContextInt,
					psRGXKickRSIN->ui32ClientCacheOpSeqNum,
					psRGXKickRSIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceSyncOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickRSIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateSyncOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickRSIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickRSIN->ui32CmdSize,
					psDMCmdInt,
					psRGXKickRSIN->ui32FCCmdSize,
					psFCDMCmdInt,
					psRGXKickRSIN->ui32FrameContext,
					psRGXKickRSIN->bbPDumpContinuous,
					psRGXKickRSIN->ui32ExtJobRef);
	PMRUnlock();




RGXKickRS_exit:
	if (psClientFenceUFOSyncPrimBlockInt)
		OSFreeMemNoStats(psClientFenceUFOSyncPrimBlockInt);
	if (hClientFenceUFOSyncPrimBlockInt2)
		OSFreeMemNoStats(hClientFenceUFOSyncPrimBlockInt2);
	if (ui32ClientFenceSyncOffsetInt)
		OSFreeMemNoStats(ui32ClientFenceSyncOffsetInt);
	if (ui32ClientFenceValueInt)
		OSFreeMemNoStats(ui32ClientFenceValueInt);
	if (psClientUpdateUFOSyncPrimBlockInt)
		OSFreeMemNoStats(psClientUpdateUFOSyncPrimBlockInt);
	if (hClientUpdateUFOSyncPrimBlockInt2)
		OSFreeMemNoStats(hClientUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientUpdateSyncOffsetInt)
		OSFreeMemNoStats(ui32ClientUpdateSyncOffsetInt);
	if (ui32ClientUpdateValueInt)
		OSFreeMemNoStats(ui32ClientUpdateValueInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMemNoStats(ui32ServerSyncFlagsInt);
	if (psServerSyncsInt)
		OSFreeMemNoStats(psServerSyncsInt);
	if (hServerSyncsInt2)
		OSFreeMemNoStats(hServerSyncsInt2);
	if (psDMCmdInt)
		OSFreeMemNoStats(psDMCmdInt);
	if (psFCDMCmdInt)
		OSFreeMemNoStats(psFCDMCmdInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXKickVRDM(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKVRDM *psRGXKickVRDMIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKVRDM *psRGXKickVRDMOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientFenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32ClientUpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = NULL;
	IMG_HANDLE *hServerSyncsInt2 = NULL;
	RGX_RPM_FREELIST * psSHFFreeListInt = NULL;
	RGX_RPM_FREELIST * psSHGFreeListInt = NULL;
	IMG_BYTE *psDMCmdInt = NULL;




	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientFenceUFOSyncPrimBlockInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
		hClientFenceUFOSyncPrimBlockInt2 = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE));
		if (!hClientFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->phClientFenceUFOSyncPrimBlock, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickVRDMIN->phClientFenceUFOSyncPrimBlock,
				psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceSyncOffsetInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceSyncOffsetInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->pui32ClientFenceSyncOffset, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceSyncOffsetInt, psRGXKickVRDMIN->pui32ClientFenceSyncOffset,
				psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceValueInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->pui32ClientFenceValue, psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickVRDMIN->pui32ClientFenceValue,
				psRGXKickVRDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
		hClientUpdateUFOSyncPrimBlockInt2 = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->phClientUpdateUFOSyncPrimBlock, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickVRDMIN->phClientUpdateUFOSyncPrimBlock,
				psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateSyncOffsetInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateSyncOffsetInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->pui32ClientUpdateSyncOffset, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateSyncOffsetInt, psRGXKickVRDMIN->pui32ClientUpdateSyncOffset,
				psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateValueInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->pui32ClientUpdateValue, psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickVRDMIN->pui32ClientUpdateValue,
				psRGXKickVRDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncFlagsInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->pui32ServerSyncFlags, psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickVRDMIN->pui32ServerSyncFlags,
				psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncsInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
		hServerSyncsInt2 = OSAllocMemNoStats(psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncsInt2)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->phServerSyncs, psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickVRDMIN->phServerSyncs,
				psRGXKickVRDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}
	if (psRGXKickVRDMIN->ui32CmdSize != 0)
	{
		psDMCmdInt = OSAllocMemNoStats(psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE));
		if (!psDMCmdInt)
		{
			psRGXKickVRDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickVRDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXKickVRDMIN->psDMCmd, psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psDMCmdInt, psRGXKickVRDMIN->psDMCmd,
				psRGXKickVRDMIN->ui32CmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickVRDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickVRDM_exit;
			}

	PMRLock();






				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											psRGXKickVRDMIN->hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickVRDMIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}
		}
	}





				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psSHFFreeListInt,
											psRGXKickVRDMIN->hSHFFreeList,
											PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psRGXKickVRDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psSHGFreeListInt,
											psRGXKickVRDMIN->hSHGFreeList,
											PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST);
					if(psRGXKickVRDMOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXKickVRDM_exit;
					}
				}

	psRGXKickVRDMOUT->eError =
		PVRSRVRGXKickVRDMKM(
					psRayContextInt,
					psRGXKickVRDMIN->ui32ClientCacheOpSeqNum,
					psRGXKickVRDMIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceSyncOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickVRDMIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateSyncOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickVRDMIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psSHFFreeListInt,
					psSHGFreeListInt,
					psRGXKickVRDMIN->ui32CmdSize,
					psDMCmdInt,
					psRGXKickVRDMIN->bbPDumpContinuous,
					psRGXKickVRDMIN->ui32ExtJobRef);
	PMRUnlock();




RGXKickVRDM_exit:
	if (psClientFenceUFOSyncPrimBlockInt)
		OSFreeMemNoStats(psClientFenceUFOSyncPrimBlockInt);
	if (hClientFenceUFOSyncPrimBlockInt2)
		OSFreeMemNoStats(hClientFenceUFOSyncPrimBlockInt2);
	if (ui32ClientFenceSyncOffsetInt)
		OSFreeMemNoStats(ui32ClientFenceSyncOffsetInt);
	if (ui32ClientFenceValueInt)
		OSFreeMemNoStats(ui32ClientFenceValueInt);
	if (psClientUpdateUFOSyncPrimBlockInt)
		OSFreeMemNoStats(psClientUpdateUFOSyncPrimBlockInt);
	if (hClientUpdateUFOSyncPrimBlockInt2)
		OSFreeMemNoStats(hClientUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientUpdateSyncOffsetInt)
		OSFreeMemNoStats(ui32ClientUpdateSyncOffsetInt);
	if (ui32ClientUpdateValueInt)
		OSFreeMemNoStats(ui32ClientUpdateValueInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMemNoStats(ui32ServerSyncFlagsInt);
	if (psServerSyncsInt)
		OSFreeMemNoStats(psServerSyncsInt);
	if (hServerSyncsInt2)
		OSFreeMemNoStats(hServerSyncsInt2);
	if (psDMCmdInt)
		OSFreeMemNoStats(psDMCmdInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXCreateRayContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATERAYCONTEXT *psRGXCreateRayContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATERAYCONTEXT *psRGXCreateRayContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_BYTE *psFrameworkCmdInt = NULL;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;




	if (psRGXCreateRayContextIN->ui32FrameworkCmdSize != 0)
	{
		psFrameworkCmdInt = OSAllocMemNoStats(psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE));
		if (!psFrameworkCmdInt)
		{
			psRGXCreateRayContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateRayContext_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (void*) psRGXCreateRayContextIN->psFrameworkCmd, psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateRayContextIN->psFrameworkCmd,
				psRGXCreateRayContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXCreateRayContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCreateRayContext_exit;
			}

	PMRLock();






				{
					/* Look up the address from the handle */
					psRGXCreateRayContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											psRGXCreateRayContextIN->hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
					if(psRGXCreateRayContextOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto RGXCreateRayContext_exit;
					}
				}

	psRGXCreateRayContextOUT->eError =
		PVRSRVRGXCreateRayContextKM(psConnection, OSGetDevData(psConnection),
					psRGXCreateRayContextIN->ui32Priority,
					psRGXCreateRayContextIN->sMCUFenceAddr,
					psRGXCreateRayContextIN->sVRMCallStackAddr,
					psRGXCreateRayContextIN->ui32FrameworkCmdSize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psRayContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto RGXCreateRayContext_exit;
	}
	PMRUnlock();






	psRGXCreateRayContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,

							&psRGXCreateRayContextOUT->hRayContext,
							(void *) psRayContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyRayContextKM);
	if (psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateRayContext_exit;
	}




RGXCreateRayContext_exit:
	if (psRGXCreateRayContextOUT->eError != PVRSRV_OK)
	{
		if (psRayContextInt)
		{
			PVRSRVRGXDestroyRayContextKM(psRayContextInt);
		}
	}

	if (psFrameworkCmdInt)
		OSFreeMemNoStats(psFrameworkCmdInt);

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyRayContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYRAYCONTEXT *psRGXDestroyRayContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYRAYCONTEXT *psRGXDestroyRayContextOUT,
					 CONNECTION_DATA *psConnection)
{





	PMRLock();








	psRGXDestroyRayContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyRayContextIN->hRayContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
	if ((psRGXDestroyRayContextOUT->eError != PVRSRV_OK) && (psRGXDestroyRayContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		PMRUnlock();
		goto RGXDestroyRayContext_exit;
	}


	PMRUnlock();


RGXDestroyRayContext_exit:

	return 0;
}


static IMG_INT
PVRSRVBridgeRGXSetRayContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETRAYCONTEXTPRIORITY *psRGXSetRayContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXSETRAYCONTEXTPRIORITY *psRGXSetRayContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_RAY_CONTEXT * psRayContextInt = NULL;











				{
					/* Look up the address from the handle */
					psRGXSetRayContextPriorityOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(void **) &psRayContextInt,
											psRGXSetRayContextPriorityIN->hRayContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT);
					if(psRGXSetRayContextPriorityOUT->eError != PVRSRV_OK)
					{
						goto RGXSetRayContextPriority_exit;
					}
				}

	psRGXSetRayContextPriorityOUT->eError =
		PVRSRVRGXSetRayContextPriorityKM(psConnection, OSGetDevData(psConnection),
					psRayContextInt,
					psRGXSetRayContextPriorityIN->ui32Priority);




RGXSetRayContextPriority_exit:

	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXRAYBridge(void);
PVRSRV_ERROR DeinitRGXRAYBridge(void);

/*
 * Register all RGXRAY functions with services
 */
PVRSRV_ERROR InitRGXRAYBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMFREELIST, PVRSRVBridgeRGXCreateRPMFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMFREELIST, PVRSRVBridgeRGXDestroyRPMFreeList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERPMCONTEXT, PVRSRVBridgeRGXCreateRPMContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRPMCONTEXT, PVRSRVBridgeRGXDestroyRPMContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXKICKRS, PVRSRVBridgeRGXKickRS,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXKICKVRDM, PVRSRVBridgeRGXKickVRDM,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXCREATERAYCONTEXT, PVRSRVBridgeRGXCreateRayContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXDESTROYRAYCONTEXT, PVRSRVBridgeRGXDestroyRayContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXRAY, PVRSRV_BRIDGE_RGXRAY_RGXSETRAYCONTEXTPRIORITY, PVRSRVBridgeRGXSetRayContextPriority,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxray functions with services
 */
PVRSRV_ERROR DeinitRGXRAYBridge(void)
{
	return PVRSRV_OK;
}

