/*
    Functions for Plug and Play and Power Management.
    Copyright (C) 2002-2015 Bo Brantén.

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

NTSTATUS
SynchronousCompletion (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
    {
        KeSetEvent((PKEVENT) Context, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ForwardIrpSynchronously (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    KEVENT              event;
    PDEVICE_EXTENSION   device_extension;
    NTSTATUS            status;

    IoCopyCurrentIrpStackLocationToNext(Irp);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    IoSetCompletionRoutine(
        Irp,
        SynchronousCompletion,
        &event,
        TRUE,
        TRUE,
        TRUE
        );

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    status = IoCallDriver(device_extension->TargetDeviceObject, Irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        status = Irp->IoStatus.Status;
    }

    return status;
}

NTSTATUS
SwapFsPnp (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PDEVICE_EXTENSION   device_extension;
    PIO_STACK_LOCATION  io_stack;
    NTSTATUS            status;
    BOOLEAN             setPageable;
    BOOLEAN             addPageFile;

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    switch (io_stack->MinorFunction)
    {
    case IRP_MN_DEVICE_USAGE_NOTIFICATION:

        if (io_stack->Parameters.UsageNotification.Type != DeviceUsageTypePaging)
        {
            status = SendIrpToNextDriver(DeviceObject, Irp);
            break;
        }

        setPageable = FALSE;
        addPageFile = io_stack->Parameters.UsageNotification.InPath;

        status = KeWaitForSingleObject(
            &device_extension->PagingPathCountEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );

        if (!addPageFile &&
            device_extension->PagingPathCount == 1 &&
            !(DeviceObject->Flags & DO_POWER_INRUSH))
        {
            DeviceObject->Flags |= DO_POWER_PAGABLE;
            setPageable = TRUE;
        }

        status = ForwardIrpSynchronously(DeviceObject, Irp);

        if (NT_SUCCESS(status))
        {
            IoAdjustPagingPathCount(
                &device_extension->PagingPathCount,
                addPageFile
                );

            if (addPageFile && device_extension->PagingPathCount == 1)
            {
                DeviceObject->Flags &= ~DO_POWER_PAGABLE;
            }
        }
        else if (setPageable)
        {
            DeviceObject->Flags &= ~DO_POWER_PAGABLE;
        }

        KeSetEvent(
            &device_extension->PagingPathCountEvent,
            IO_NO_INCREMENT,
            FALSE
            );

        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        break;

    default:
        status = SendIrpToNextDriver(DeviceObject, Irp);
    }

    return status;
}

NTSTATUS
SwapFsPower (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PDEVICE_EXTENSION device_extension;

    PoStartNextPowerIrp(Irp);

    IoSkipCurrentIrpStackLocation(Irp);

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    return PoCallDriver(device_extension->TargetDeviceObject, Irp);
}
