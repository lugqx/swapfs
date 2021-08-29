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

#ifndef SWAPFS_H
#define SWAPFS_H

#define SWAPFS_POOL_TAG 'pawS'

typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT  TargetDeviceObject;
    KEVENT          PagingPathCountEvent;
    LONG            PagingPathCount;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#ifdef _PREFAST_
DRIVER_INITIALIZE DriverEntry;
__drv_dispatchType(IRP_MJ_CREATE) __drv_dispatchType(IRP_MJ_CLOSE) __drv_dispatchType(IRP_MJ_FLUSH_BUFFERS) __drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) __drv_dispatchType(IRP_MJ_SHUTDOWN) __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL) DRIVER_DISPATCH SendIrpToNextDriver;
__drv_dispatchType(IRP_MJ_READ) __drv_dispatchType(IRP_MJ_WRITE) DRIVER_DISPATCH SwapFsReadWrite;
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH SwapFsDeviceControl;
__drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH SwapFsPnp;
__drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH SwapFsPower;
IO_COMPLETION_ROUTINE DeviceControlCompletion;
IO_COMPLETION_ROUTINE SynchronousCompletion;
#endif // _PREFAST_

NTSTATUS
DriverEntry (
    IN PDRIVER_OBJECT   DriverObject,
    IN PUNICODE_STRING  RegistryPath
    );

NTSTATUS
SwapFsFindDevice (
    IN PDRIVER_OBJECT   DriverObject,
    IN PUNICODE_STRING  RegistryPath,
    IN ULONG            DeviceNumber
    );

NTSTATUS
SendIrpToNextDriver (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
SwapFsReadWrite (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
DeviceControlCompletion (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    );

NTSTATUS
SwapFsDeviceControl (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
SynchronousCompletion (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    );

NTSTATUS
ForwardIrpSynchronously (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
SwapFsPnp (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
SwapFsPower (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    );

NTSTATUS
IsDeviceLinuxSwap (
    IN PDEVICE_OBJECT DeviceObject
    );

NTSTATUS
FormatDeviceToFat (
    IN PDEVICE_OBJECT DeviceObject
    );

NTSTATUS
FormatDeviceToFat32 (
    IN PDEVICE_OBJECT DeviceObject
    );

NTSTATUS
ReadBlockDevice (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PLARGE_INTEGER   Offset,
    IN ULONG            Length,
    IN OUT PVOID        Buffer
    );

NTSTATUS
WriteBlockDevice (
    IN PDEVICE_OBJECT   DeviceObject,
    IN PLARGE_INTEGER   Offset,
    IN ULONG            Length,
    IN PVOID            Buffer
    );

NTSTATUS 
BlockDeviceIoControl (
    IN PDEVICE_OBJECT   DeviceObject,
    IN ULONG            IoctlCode,
    IN PVOID            InputBuffer,
    IN ULONG            InputBufferSize,
    IN OUT PVOID        OutputBuffer,
    IN OUT PULONG       OutputBufferSize
    );

#endif /* SWAPFS_H */
