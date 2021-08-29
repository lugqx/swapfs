/*
    Functions for synchronous read, write and ioctl on a device.
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

#ifdef ALLOC_PRAGMA
#pragma alloc_text("INIT", ReadBlockDevice)
#pragma alloc_text("INIT", WriteBlockDevice)
#pragma alloc_text("INIT", BlockDeviceIoControl)
#endif // ALLOC_PRAGMA

NTSTATUS
ReadBlockDevice (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PLARGE_INTEGER   Offset,
    IN ULONG            Length,
    OUT PVOID           Buffer
    )
{
    KEVENT          Event;
    PIRP            Irp;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS        Status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Offset != NULL);
    ASSERT(Buffer != NULL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        DeviceObject,
        Buffer,
        Length,
        Offset,
        &Event,
        &IoStatus
        );

    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(
            &Event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS
WriteBlockDevice (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PLARGE_INTEGER   Offset,
    IN ULONG            Length,
    IN PVOID            Buffer
    )
{
    KEVENT          Event;
    PIRP            Irp;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS        Status;

    ASSERT(DeviceObject != NULL);
    ASSERT(Offset != NULL);
    ASSERT(Buffer != NULL);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_WRITE,
        DeviceObject,
        Buffer,
        Length,
        Offset,
        &Event,
        &IoStatus
        );

    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(
            &Event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS 
BlockDeviceIoControl (
    IN PDEVICE_OBJECT   DeviceObject,
    IN ULONG            IoctlCode,
    IN PVOID            InputBuffer,
    IN ULONG            InputBufferSize,
    OUT PVOID           OutputBuffer,
    IN OUT PULONG       OutputBufferSize
    )
{
    ULONG           OutputBufferSize2 = 0;
    KEVENT          Event;
    PIRP            Irp;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS        Status;

    ASSERT(DeviceObject != NULL);

    if (OutputBufferSize)
    {
        OutputBufferSize2 = *OutputBufferSize;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(
        IoctlCode,
        DeviceObject,
        InputBuffer,
        InputBufferSize,
        OutputBuffer,
        OutputBufferSize2,
        FALSE,
        &Event,
        &IoStatus
        );

    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(
            &Event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        Status = IoStatus.Status;
    }

    if (OutputBufferSize)
    {
        *OutputBufferSize = (ULONG) IoStatus.Information;
    }

    return Status;
}
