/*
    Function to recognize a Linux swap partition.
    Copyright (C) 1999-2015 Bo Brantén.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ntddk.h>
#include "swapfs.h"
#include "swap.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text("INIT", IsDeviceLinuxSwap)
#endif // ALLOC_PRAGMA

NTSTATUS
IsDeviceLinuxSwap (
    IN PDEVICE_OBJECT DeviceObject
    )
{
    union swap_header*  SwapHeader;
    LARGE_INTEGER       Offset;
    NTSTATUS            Status;

    ASSERT(DeviceObject != NULL);

    SwapHeader = (union swap_header*) ExAllocatePoolWithTag(PagedPool, sizeof(union swap_header), SWAPFS_POOL_TAG);

    if (!SwapHeader)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Offset.QuadPart = SWAP_HEADER_OFFSET;

    Status = ReadBlockDevice(
        DeviceObject,
        &Offset,
        sizeof(union swap_header),
        SwapHeader
        );

    if (!NT_SUCCESS(Status) ||
        (RtlCompareMemory(SwapHeader->magic.magic, SWAP_HEADER_MAGIC_V1, 10) != 10 &&
         RtlCompareMemory(SwapHeader->magic.magic, SWAP_HEADER_MAGIC_V2, 10) != 10))
    {
        Status = STATUS_UNRECOGNIZED_VOLUME;
    }

    ExFreePool(SwapHeader);

    return Status;
}
