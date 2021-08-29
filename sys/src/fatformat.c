/*
    Function to format a device to FAT16 or FAT12.
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
#include <ntdddisk.h>
#include "swapfs.h"
#include "swap.h"
#include "fat.h"

#define ROOT_DIR_ENTRYS 512

#ifdef ALLOC_PRAGMA
#pragma alloc_text("INIT", FormatDeviceToFat)
#endif // ALLOC_PRAGMA

NTSTATUS
FormatDeviceToFat (
    IN PDEVICE_OBJECT DeviceObject
    )
{
    ULONG                       size;
    NTSTATUS                    status;
    DISK_GEOMETRY               disk_geometry;
    PARTITION_INFORMATION       partition_information;
    PARTITION_INFORMATION_EX    partition_information_ex;
    USHORT                      sector_size;
    PUCHAR                      buffer;
    struct msdos_boot_sector*   boot_sector;
    ULONG                       nsector;
    ULONG                       ncluster;
    ULONG                       cluster_size;
    ULONG                       fat_type;
    ULONG                       fat_length;
    LARGE_INTEGER               offset;
    ULONG                       n;
    struct msdos_dir_entry*     root_dir;

    ASSERT(DeviceObject != NULL);

    size = sizeof(disk_geometry);

    status = BlockDeviceIoControl(
        DeviceObject,
        IOCTL_DISK_GET_DRIVE_GEOMETRY,
        NULL,
        0,
        &disk_geometry,
        &size
        );

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    size = sizeof(partition_information);

    /* IOCTL_DISK_GET_PARTITION_INFO is only supported on disks with MBR,
       retry IOCTL_DISK_GET_PARTITION_INFO_EX on disks with GPT. */

    status = BlockDeviceIoControl(
        DeviceObject,
        IOCTL_DISK_GET_PARTITION_INFO,
        NULL,
        0,
        &partition_information,
        &size
        );

    if (!NT_SUCCESS(status))
    {
        size = sizeof(partition_information_ex);

        status = BlockDeviceIoControl(
            DeviceObject,
            IOCTL_DISK_GET_PARTITION_INFO_EX,
            NULL,
            0,
            &partition_information_ex,
            &size
            );

        if (!NT_SUCCESS(status))
        {
            return status;
        }

        partition_information.PartitionLength.QuadPart = partition_information_ex.PartitionLength.QuadPart;
    }

    sector_size = (USHORT) disk_geometry.BytesPerSector;

    if (!sector_size)
    {
        return STATUS_INVALID_PARAMETER;
    }

    buffer = (PUCHAR) ExAllocatePoolWithTag(PagedPool, sector_size, SWAPFS_POOL_TAG);

    if (!buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(buffer, sector_size);

    boot_sector = (struct msdos_boot_sector*) buffer;

    boot_sector->boot_jump[0] = 0xeb;
    boot_sector->boot_jump[1] = 0x3c;
    boot_sector->boot_jump[2] = 0x90;

    RtlCopyMemory(boot_sector->system_id, "Swap    ", 8);

    *(PUSHORT)boot_sector->sector_size = sector_size;

    boot_sector->reserved = 1;

    boot_sector->fats = 1;

    *(PUSHORT)boot_sector->dir_entries = ROOT_DIR_ENTRYS;

    nsector = (ULONG) ((partition_information.PartitionLength.QuadPart -
        sizeof(union swap_header)) / sector_size);

    if (nsector <= 0xffff)
    {
        *(PUSHORT)boot_sector->sectors = (USHORT) nsector;
    }
    else
    {
        boot_sector->total_sect = nsector;
    }

    boot_sector->cluster_size = 1;
    ncluster = nsector;

    while (ncluster > 0xffff && boot_sector->cluster_size <= 128)
    {
        boot_sector->cluster_size <<= 1;
        if (boot_sector->cluster_size == 0)
        {
            boot_sector->cluster_size = 128;
            ncluster = 65517;
            break;
        }
        ncluster = nsector / boot_sector->cluster_size;
    }

    cluster_size = boot_sector->cluster_size;

    boot_sector->media = 0xf8;

    if (ncluster > 4087)
    {
        fat_type = 16;

        fat_length = (ncluster * 2 + sector_size - 1) / sector_size;

        RtlCopyMemory(boot_sector->fs_type, MSDOS_FAT16_SIGN, 8);
    }
    else
    {
        fat_type = 12;

        fat_length = (((ncluster * 3 + 1) / 2) + sector_size - 1) / sector_size;

        RtlCopyMemory(boot_sector->fs_type, MSDOS_FAT12_SIGN, 8);
    }

    boot_sector->fat_length = (USHORT) fat_length;

    boot_sector->secs_track = (USHORT) disk_geometry.SectorsPerTrack;

    boot_sector->heads = (USHORT) disk_geometry.TracksPerCylinder;

    boot_sector->ext_boot_sign = MSDOS_EXT_SIGN;

    *(PULONG)boot_sector->volume_id = 0x19990502;

    RtlCopyMemory(boot_sector->volume_label, "Linux Swap ", 11);

    boot_sector->boot_sign = BOOT_SIGN;

    offset.QuadPart = sizeof(union swap_header);

    status = WriteBlockDevice(
        DeviceObject,
        &offset,
        sector_size,
        buffer
        );

    if (!NT_SUCCESS(status))
    {
        ExFreePool(buffer);
        return status;
    }

    RtlZeroMemory(buffer, sector_size);

    buffer[0] = 0xf8;
    buffer[1] = 0xff;
    buffer[2] = 0xff;

    if (fat_type == 16)
    {
        buffer[3] = 0xff;
    }

    offset.QuadPart += sector_size;

    status = WriteBlockDevice(
        DeviceObject,
        &offset,
        sector_size,
        buffer
        );

    if (!NT_SUCCESS(status))
    {
        ExFreePool(buffer);
        return status;
    }

    RtlZeroMemory(buffer, sector_size);

    offset.QuadPart += sector_size;

    for (n = 0;
         n < fat_length - 1;
         n++, offset.QuadPart += sector_size
        )
    {
        status = WriteBlockDevice(
            DeviceObject,
            &offset,
            sector_size,
            buffer
            );

        if (!NT_SUCCESS(status))
        {
            ExFreePool(buffer);
            return status;
        }
    }

    root_dir = (struct msdos_dir_entry*) buffer;

    RtlCopyMemory(root_dir->name, "Swap    ", 8);
    RtlCopyMemory(root_dir->ext, "   ", 3);
    root_dir->attr = ATTR_VOLUME;

    status = WriteBlockDevice(
        DeviceObject,
        &offset,
        sector_size,
        buffer
        );

    if (!NT_SUCCESS(status))
    {
        ExFreePool(buffer);
        return status;
    }

    RtlZeroMemory(buffer, sector_size);

    offset.QuadPart += sector_size;

    for (n = 0;
         n < (sizeof(struct msdos_dir_entry) * ROOT_DIR_ENTRYS / sector_size);
         n++, offset.QuadPart += sector_size
        )
    {
        status = WriteBlockDevice(
            DeviceObject,
            &offset,
            sector_size,
            buffer
            );

        if (!NT_SUCCESS(status))
        {
            ExFreePool(buffer);
            return status;
        }
    }

    ExFreePool(buffer);

    KdPrint(("SwapFs: Device size is %uMB having %u sectors of %u bytes formated to FAT%u using %u clusters of %u sectors.\n",
        (ULONG) ((partition_information.PartitionLength.QuadPart -
        sizeof(union swap_header)) / 0x100000),
        nsector, sector_size, fat_type, ncluster, cluster_size));

    return STATUS_SUCCESS;
}
