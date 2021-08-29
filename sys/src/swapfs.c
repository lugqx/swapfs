/*
    This is a disk filter driver for Windows that uses a Linux swap partition
    to provide a temporary storage area formated to the FAT file system.
    Copyright (C) 1999-2016 Bo Brantén.

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
#include <ntdddisk.h>
#include <ntstrsafe.h>
#include "swapfs.h"
#include "swap.h"

#define PARAMETER_KEY       L"\\Parameters"
#define SWAPDEVICE_VALUE    L"SwapDevice"

#ifdef ALLOC_PRAGMA
#pragma alloc_text("INIT", DriverEntry)
#pragma alloc_text("INIT", SwapFsFindDevice)
#endif // ALLOC_PRAGMA

NTSTATUS
DriverEntry (
    IN PDRIVER_OBJECT   DriverObject,
    IN PUNICODE_STRING  RegistryPath
    )
{
    ULONG       n, n_found_devices;
    NTSTATUS    status;

    DriverObject->MajorFunction[IRP_MJ_READ]                    = SwapFsReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE]                   = SwapFsReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = SwapFsDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]                     = SwapFsPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER]                   = SwapFsPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE]                  = SendIrpToNextDriver;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = SendIrpToNextDriver;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]           = SendIrpToNextDriver;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SendIrpToNextDriver;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN]                = SendIrpToNextDriver;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL]          = SendIrpToNextDriver;

    /* search for the swap partitions the user has listed */

    for (n = 0, n_found_devices = 0; n < 10; n++)
    {
        status = SwapFsFindDevice(DriverObject, RegistryPath, n);

        if (NT_SUCCESS(status))
        {
            n_found_devices++;
        }
        /* skip an unrecognized partition but break if the user has not listed any more to search */
        else if(n && status != STATUS_UNRECOGNIZED_VOLUME)
        {
            break;
        }
    }

    if (n_found_devices == 0)
    {
        KdPrint(("SwapFs: No Linux swap device found, driver not loaded.\n"));
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
SwapFsFindDevice (
    IN PDRIVER_OBJECT   DriverObject,
    IN PUNICODE_STRING  RegistryPath,
    IN ULONG            DeviceNumber
    )
{
    UNICODE_STRING              parameter_path;
    UNICODE_STRING              parameter_name;
    UNICODE_STRING              device_name;
    RTL_QUERY_REGISTRY_TABLE    query_table[2];
    NTSTATUS                    status;
    PDEVICE_OBJECT              device_object;
    PDEVICE_EXTENSION           device_extension;

    /* read SwapDevice and SwapDevice1 to SwapDeviceN in [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\SwapFs\Parameters] */

    parameter_path.Length = 0;

    parameter_path.MaximumLength = RegistryPath->Length + sizeof(PARAMETER_KEY) + sizeof(WCHAR);

    parameter_path.Buffer = (PWSTR) ExAllocatePoolWithTag(PagedPool, parameter_path.MaximumLength, SWAPFS_POOL_TAG);

    if (!parameter_path.Buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyUnicodeString(&parameter_path, RegistryPath);

    RtlAppendUnicodeToString(&parameter_path, PARAMETER_KEY);

    parameter_name.Length = 0;

    parameter_name.MaximumLength = sizeof(L"SwapDevice") + sizeof(WCHAR);

    parameter_name.Buffer = (PWCHAR) ExAllocatePoolWithTag(PagedPool, sizeof(L"SwapDevice") + sizeof(WCHAR), SWAPFS_POOL_TAG);

    if (!parameter_name.Buffer)
    {
        ExFreePool(parameter_path.Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* The driver will search for the strings SwapDevice and SwapDevice1 to SwapDeviceN */

    if (DeviceNumber)
    {
        RtlUnicodeStringPrintf(&parameter_name, SWAPDEVICE_VALUE L"%u", DeviceNumber);
    }
    else
    {
        RtlUnicodeStringPrintf(&parameter_name, SWAPDEVICE_VALUE);
    }

    device_name.Length = 0;
    device_name.MaximumLength = 0;
    device_name.Buffer = NULL;

    RtlZeroMemory(&query_table[0], sizeof(query_table));

    query_table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    query_table[0].Name = parameter_name.Buffer;
    query_table[0].EntryContext = &device_name;

    status = RtlQueryRegistryValues(
        RTL_REGISTRY_ABSOLUTE,
        parameter_path.Buffer,
        &query_table[0],
        NULL,
        NULL
        );

    ExFreePool(parameter_path.Buffer);
    ExFreePool(parameter_name.Buffer);

    if (!NT_SUCCESS(status) || !device_name.Buffer)
    {
        if (DeviceNumber) { KdPrint(("SwapFs: No more swap devices to search.\n")); }
        return STATUS_UNSUCCESSFUL;
    }

    /* create a device object and attach to the target partition */

    status = IoCreateDevice(
        DriverObject,
        sizeof(DEVICE_EXTENSION),
        NULL,
        FILE_DEVICE_DISK,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &device_object
        );

    if (!NT_SUCCESS(status))
    {
        KdPrint(("SwapFs: Create device failed.\n"));
        ExFreePool(device_name.Buffer);
        return status;
    }

    device_extension = (PDEVICE_EXTENSION) device_object->DeviceExtension;

    KeInitializeEvent(&device_extension->PagingPathCountEvent, NotificationEvent, TRUE);

    device_extension->PagingPathCount = 0;

    status = IoAttachDevice(
        device_object,
        &device_name,
        &device_extension->TargetDeviceObject
        );

    ExFreePool(device_name.Buffer);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("SwapFs: Attach device failed.\n"));
        IoDeleteDevice(device_object);
        return status;
    }

    device_object->Flags |= (device_extension->TargetDeviceObject->Flags &
        (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE));

    device_object->Characteristics |=
        (device_extension->TargetDeviceObject->Characteristics &
         FILE_CHARACTERISTICS_PROPAGATED);

    /* check that it is a Linux swap partition and preformat it to FAT */

    status = IsDeviceLinuxSwap(device_extension->TargetDeviceObject);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("SwapFs: Not a Linux swap device.\n"));
        IoDetachDevice(device_extension->TargetDeviceObject);
        IoDeleteDevice(device_object);
        return status;
    }

    status = FormatDeviceToFat32(device_extension->TargetDeviceObject);

    if (!NT_SUCCESS(status))
    {
        KdPrint(("SwapFs: FormatDeviceToFat32 failed, trying FormatDeviceToFat...\n"));
        status = FormatDeviceToFat(device_extension->TargetDeviceObject);
    }

    if (!NT_SUCCESS(status))
    {
        KdPrint(("SwapFs: FormatDeviceToFat failed.\n"));
        IoDetachDevice(device_extension->TargetDeviceObject);
        IoDeleteDevice(device_object);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
SendIrpToNextDriver (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PDEVICE_EXTENSION device_extension;

    IoSkipCurrentIrpStackLocation(Irp);

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    return IoCallDriver(device_extension->TargetDeviceObject, Irp);
}

NTSTATUS
SwapFsReadWrite (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PIO_STACK_LOCATION  next_io_stack;
    PDEVICE_EXTENSION   device_extension;

    IoCopyCurrentIrpStackLocationToNext(Irp);

    next_io_stack = IoGetNextIrpStackLocation(Irp);

    next_io_stack->Parameters.Read.ByteOffset.QuadPart += sizeof(union swap_header);

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    return IoCallDriver(device_extension->TargetDeviceObject, Irp);
}

NTSTATUS
DeviceControlCompletion (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
{
    PIO_STACK_LOCATION io_stack;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    switch (io_stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_DISK_GET_PARTITION_INFO:
        {
        PPARTITION_INFORMATION p;
        p = (PPARTITION_INFORMATION) Irp->AssociatedIrp.SystemBuffer;
        ASSERT(p != NULL);
        p->PartitionLength.QuadPart -= sizeof(union swap_header);
        break;
        }
    case IOCTL_DISK_GET_PARTITION_INFO_EX:
        {
        PPARTITION_INFORMATION_EX p;
        p = (PPARTITION_INFORMATION_EX) Irp->AssociatedIrp.SystemBuffer;
        ASSERT(p != NULL);
        p->PartitionLength.QuadPart -= sizeof(union swap_header);
        break;
        }
    case IOCTL_DISK_GET_LENGTH_INFO:
        {
        PGET_LENGTH_INFORMATION p;
        p = (PGET_LENGTH_INFORMATION) Irp->AssociatedIrp.SystemBuffer;
        ASSERT(p != NULL);
        p->Length.QuadPart -= sizeof(union swap_header);
        break;
        }
    }

    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
SwapFsDeviceControl (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PIO_STACK_LOCATION  io_stack;
    PDEVICE_EXTENSION   device_extension;

    io_stack = IoGetCurrentIrpStackLocation(Irp);

    if (io_stack->Parameters.DeviceIoControl.IoControlCode ==
        IOCTL_DISK_SET_PARTITION_INFO
        ||
        io_stack->Parameters.DeviceIoControl.IoControlCode ==
        IOCTL_DISK_SET_PARTITION_INFO_EX
        )
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IoCopyCurrentIrpStackLocationToNext(Irp);

    IoSetCompletionRoutine(
        Irp,
        DeviceControlCompletion,
        DeviceObject,
        TRUE,
        FALSE,
        FALSE
        );

    device_extension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

    return IoCallDriver(device_extension->TargetDeviceObject, Irp);
}
