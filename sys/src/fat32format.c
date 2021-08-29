/*
    Function to format a device to FAT32.
    Copyright (C) 2010-2016 Bo Brantén.

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

// Fat32 formatter version 1.05
// (c) Tom Thornhill 2007,2008,2009
// Modified for Windows kernel mode by Bo Brantén 2010.
// This software is covered by the GPL.
// By using this tool, you agree to absolve Ridgecrop of an liabilities for lost data.
// Please backup any data you value before using this tool.

#include <ntddk.h>
#include <ntdddisk.h>
#include "swapfs.h"
#include "swap.h"
#include "fat.h"
#include "fat32.h"

#if DBG
#define die(x) { DbgPrint(x); return -1; }
#else // !DBG
#define die(x) { return -1; }
#endif // !DBG

#pragma code_seg("INIT")

static void *malloc(size_t size)
{
    return ExAllocatePoolWithTag(PagedPool, size, SWAPFS_POOL_TAG);
}

static void free(void *ptr)
{
    ExFreePool(ptr);
}

/*
This is the Microsoft calculation from FATGEN

    DWORD RootDirSectors = 0;
    DWORD TmpVal1, TmpVal2, FATSz;

    TmpVal1 = DskSize - ( ReservedSecCnt + RootDirSectors);
    TmpVal2 = (256 * SecPerClus) + NumFATs;
    TmpVal2 = TmpVal2 / 2;
    FATSz = (TmpVal1 + (TmpVal2 - 1)) / TmpVal2;

    return( FatSz );
*/

static DWORD get_fat_size_sectors ( DWORD DskSize, DWORD ReservedSecCnt, DWORD SecPerClus, DWORD NumFATs, DWORD BytesPerSect )
{
    ULONGLONG   Numerator, Denominator;
    ULONGLONG   FatElementSize = 4;
    ULONGLONG   FatSz;

    // This is based on
    // http://hjem.get2net.dk/rune_moeller_barnkob/filesystems/fat.html
    // I've made the obvious changes for FAT32
    Numerator = FatElementSize * ( DskSize - ReservedSecCnt );
    Denominator = ( SecPerClus * BytesPerSect ) + ( FatElementSize * NumFATs );
    FatSz = Numerator / Denominator;
    // round up
    FatSz += 1;

    return( (DWORD) FatSz );
}

static int write_sect ( HANDLE hDevice, DWORD Sector, DWORD BytesPerSector, void *Data, DWORD NumSects )
{
    LARGE_INTEGER offset;

    offset.QuadPart = Sector * BytesPerSector + sizeof(union swap_header);

    return WriteBlockDevice(
        hDevice,
        &offset,
        NumSects*BytesPerSector,
        Data
        );
}

static int zero_sectors ( HANDLE hDevice, DWORD Sector, DWORD BytesPerSect, DWORD NumSects )
{
    BYTE *pZeroSect;
    DWORD BurstSize;
    DWORD WriteSize;
#if DBG
    LONGLONG qBytesTotal=NumSects*BytesPerSect;
#endif // DBG
    NTSTATUS status;

    BurstSize = 128; // 64K

    pZeroSect = (BYTE*) malloc(BytesPerSect*BurstSize);

    memset(pZeroSect, 0, BytesPerSect*BurstSize);

    while ( NumSects )
    {
        if ( NumSects > BurstSize )
            WriteSize = BurstSize;
        else
            WriteSize = NumSects;

        status = write_sect(hDevice, Sector, BytesPerSect, pZeroSect, WriteSize );

        Sector += WriteSize;

        if ( status ) {
            free(pZeroSect);
            die ( "Failed to write" );
            }

        NumSects -= WriteSize;
    }

    free(pZeroSect);

    KdPrint (( "SwapFs: Wrote %I64d bytes.\n", qBytesTotal ));

    return 0;
}

static BYTE get_spc ( DWORD ClusterSizeKB, DWORD BytesPerSect )
{
    DWORD spc = ( ClusterSizeKB * 1024 ) / BytesPerSect;
    return( (BYTE) spc );
}

static BYTE get_sectors_per_cluster ( LONGLONG DiskSizeBytes, DWORD BytesPerSect )
{
    BYTE ret = 0x01; // 1 sector per cluster
    LONGLONG DiskSizeMB = DiskSizeBytes / ( 1024*1024 );

    // 512 MB to 8,191 MB 4 KB
    if ( DiskSizeMB > 512 )
        ret = get_spc( 4, BytesPerSect );  // ret = 0x8;

    // 8,192 MB to 16,383 MB 8 KB
    if ( DiskSizeMB > 8192 )
        ret = get_spc( 8, BytesPerSect ); // ret = 0x10;

    // 16,384 MB to 32,767 MB 16 KB
    if ( DiskSizeMB > 16384 )
        ret = get_spc( 16, BytesPerSect ); // ret = 0x20;

    // Larger than 32,768 MB 32 KB
    if ( DiskSizeMB > 32768 )
        ret = get_spc( 32, BytesPerSect );  // ret = 0x40;

    return( ret );
}

NTSTATUS
FormatDeviceToFat32 (
    IN PDEVICE_OBJECT DeviceObject
    )
{
    HANDLE hDevice = DeviceObject;
    ULONG                       size;
    NTSTATUS                    status;
    // First open the device
    DWORD i;
    DISK_GEOMETRY dgDrive;
    PARTITION_INFORMATION piDrive;
    PARTITION_INFORMATION_EX piDriveEx;
    // Recommended values
    DWORD ReservedSectCount = 32;
    DWORD NumFATs = 2;
    DWORD BackupBootSect = 6;
    DWORD VolumeId=0; // calculated before format

    // // Calculated later
    DWORD FatSize=0;
    DWORD BytesPerSect=0;
    DWORD SectorsPerCluster=0;
    DWORD TotalSectors=0;
    DWORD SystemAreaSize=0;
    DWORD UserAreaSize=0;
    ULONGLONG qTotalSectors=0;

    // structures to be written to the disk
    FAT_BOOTSECTOR32 *pFAT32BootSect;
    FAT_FSINFO *pFAT32FsInfo;
    struct msdos_dir_entry* root_dir;

    DWORD *pFirstSectOfFat;

    BYTE VolId[12] = "Linux Swap ";

    // Debug temp vars
    ULONGLONG FatNeeded, ClusterCount;

    ASSERT(DeviceObject != NULL);

    VolumeId = 0x20100721;

    // work out drive params

    size = sizeof(dgDrive);

    status = BlockDeviceIoControl(
        hDevice,
        IOCTL_DISK_GET_DRIVE_GEOMETRY,
        NULL,
        0,
        &dgDrive,
        &size
        );

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    size = sizeof(piDrive);

    /* IOCTL_DISK_GET_PARTITION_INFO is only supported on disks with MBR,
       retry IOCTL_DISK_GET_PARTITION_INFO_EX on disks with GPT. */

    status = BlockDeviceIoControl(
        hDevice,
        IOCTL_DISK_GET_PARTITION_INFO,
        NULL,
        0,
        &piDrive,
        &size
        );

    if (!NT_SUCCESS(status))
    {
        size = sizeof(piDriveEx);

        status = BlockDeviceIoControl(
            hDevice,
            IOCTL_DISK_GET_PARTITION_INFO_EX,
            NULL,
            0,
            &piDriveEx,
            &size
            );

        if (!NT_SUCCESS(status))
        {
            return status;
        }

        piDrive.PartitionLength.QuadPart = piDriveEx.PartitionLength.QuadPart;
        piDrive.HiddenSectors = 0;
    }

    piDrive.PartitionLength.QuadPart -= sizeof(union swap_header);

    // Only support hard disks at the moment
    //if ( dgDrive.BytesPerSector != 512 )
    //{
    //    die ( "This version of fat32format only supports hard disks with 512 bytes per sector.\n" );
    //}
    BytesPerSect = dgDrive.BytesPerSector;

    // Checks on Disk Size
    qTotalSectors = piDrive.PartitionLength.QuadPart/dgDrive.BytesPerSector;
    // low end limit - 65536 sectors
    if ( qTotalSectors < 65536 )
    {
        // I suspect that most FAT32 implementations would mount this volume just fine, but the
        // spec says that we shouldn't do this, so we won't
        die ( "This drive is too small for FAT32 - there must be at least 64K clusters\n" );
    }

    if ( qTotalSectors >= 0xffffffff )
    {
        // This is a more fundamental limitation on FAT32 - the total sector count in the root dir
        // ís 32bit. With a bit of creativity, FAT32 could be extended to handle at least 2^28 clusters
        // There would need to be an extra field in the FSInfo sector, and the old sector count could
        // be set to 0xffffffff. This is non standard though, the Windows FAT driver FASTFAT.SYS won't
        // understand this. Perhaps a future version of FAT32 and FASTFAT will handle this.
        die ( "This drive is too big for FAT32 - max 2TB supported\n" );
    }

    pFAT32BootSect = (FAT_BOOTSECTOR32*) malloc(BytesPerSect);
    memset(pFAT32BootSect, 0, BytesPerSect);

    if (!pFAT32BootSect) {
        return -1;
    }

    pFAT32FsInfo = (FAT_FSINFO*) malloc(BytesPerSect);
    memset(pFAT32FsInfo, 0, BytesPerSect);

    if (!pFAT32FsInfo) {
        free(pFAT32BootSect);
        return -1;
    }

    pFirstSectOfFat = (DWORD*) malloc(BytesPerSect);
    memset(pFirstSectOfFat, 0, BytesPerSect);

    if (!pFirstSectOfFat) {
        free(pFAT32BootSect);
        free(pFAT32FsInfo);
        return -1;
    }

    // fill out the boot sector and fs info
    pFAT32BootSect->sJmpBoot[0]=0xEB;
    pFAT32BootSect->sJmpBoot[1]=0x5A;
    pFAT32BootSect->sJmpBoot[2]=0x90;
    memcpy( pFAT32BootSect->sOEMName, "MSWIN4.1", 8 );
    pFAT32BootSect->wBytsPerSec = (WORD) BytesPerSect;

    SectorsPerCluster = get_sectors_per_cluster( piDrive.PartitionLength.QuadPart, BytesPerSect );

    pFAT32BootSect->bSecPerClus = (BYTE) SectorsPerCluster ;
    pFAT32BootSect->wRsvdSecCnt = (WORD) ReservedSectCount;
    pFAT32BootSect->bNumFATs = (BYTE) NumFATs;
    pFAT32BootSect->wRootEntCnt = 0;
    pFAT32BootSect->wTotSec16 = 0;
    pFAT32BootSect->bMedia = 0xF8;
    pFAT32BootSect->wFATSz16 = 0;
    pFAT32BootSect->wSecPerTrk = (WORD) dgDrive.SectorsPerTrack;
    pFAT32BootSect->wNumHeads = (WORD) dgDrive.TracksPerCylinder;
    pFAT32BootSect->dHiddSec = (DWORD) piDrive.HiddenSectors;
    TotalSectors = (DWORD)  (piDrive.PartitionLength.QuadPart/dgDrive.BytesPerSector);
    pFAT32BootSect->dTotSec32 = TotalSectors;

    FatSize = get_fat_size_sectors ( pFAT32BootSect->dTotSec32, pFAT32BootSect->wRsvdSecCnt, pFAT32BootSect->bSecPerClus, pFAT32BootSect->bNumFATs, BytesPerSect );

    pFAT32BootSect->dFATSz32 = FatSize;
    pFAT32BootSect->wExtFlags = 0;
    pFAT32BootSect->wFSVer = 0;
    pFAT32BootSect->dRootClus = 2;
    pFAT32BootSect->wFSInfo = 1;
    pFAT32BootSect->wBkBootSec = (WORD) BackupBootSect;
    pFAT32BootSect->bDrvNum = 0x80;
    pFAT32BootSect->Reserved1 = 0;
    pFAT32BootSect->bBootSig = 0x29;

    pFAT32BootSect->dBS_VolID = VolumeId;
    memcpy( pFAT32BootSect->sVolLab, VolId, 11 );
    memcpy( pFAT32BootSect->sBS_FilSysType, "FAT32   ", 8 );
    ((BYTE*)pFAT32BootSect)[510] = 0x55;
    ((BYTE*)pFAT32BootSect)[511] = 0xaa;

    /* FATGEN103.DOC says "NOTE: Many FAT documents mistakenly say that this 0xAA55 signature occupies the "last 2 bytes of
    the boot sector". This statement is correct if - and only if - BPB_BytsPerSec is 512. If BPB_BytsPerSec is greater than
    512, the offsets of these signature bytes do not change (although it is perfectly OK for the last two bytes at the end
    of the boot sector to also contain this signature)."
    
    Windows seems to only check the bytes at offsets 510 and 511. Other OSs might check the ones at the end of the sector,
    so we'll put them there too.
    */
    if ( BytesPerSect != 512 )
        {
        ((BYTE*)pFAT32BootSect)[BytesPerSect-2] = 0x55;
        ((BYTE*)pFAT32BootSect)[BytesPerSect-1] = 0xaa;
        }

    // FSInfo sect
    pFAT32FsInfo->dLeadSig = 0x41615252;
    pFAT32FsInfo->dStrucSig = 0x61417272;
    pFAT32FsInfo->dFree_Count = (DWORD) -1;
    pFAT32FsInfo->dNxt_Free = (DWORD) -1;
    pFAT32FsInfo->dTrailSig = 0xaa550000;

    // First FAT Sector
    pFirstSectOfFat[0] = 0x0ffffff8;  // Reserved cluster 1 media id in low byte
    pFirstSectOfFat[1] = 0x0fffffff;  // Reserved cluster 2 EOC
    pFirstSectOfFat[2] = 0x0fffffff;  // end of cluster chain for root dir

    // Write boot sector, fats
    // Sector 0 Boot Sector
    // Sector 1 FSInfo 
    // Sector 2 More boot code - we write zeros here
    // Sector 3 unused
    // Sector 4 unused
    // Sector 5 unused
    // Sector 6 Backup boot sector
    // Sector 7 Backup FSInfo sector
    // Sector 8 Backup 'more boot code'
    // zero'd sectors upto ReservedSectCount
    // FAT1  ReservedSectCount to ReservedSectCount + FatSize
    // ...
    // FATn  ReservedSectCount to ReservedSectCount + FatSize
    // RootDir - allocated to cluster2

    UserAreaSize = TotalSectors - ReservedSectCount - (NumFATs*FatSize);
    ClusterCount = UserAreaSize/SectorsPerCluster;

    // Sanity check for a cluster count of >2^28, since the upper 4 bits of the cluster values in
    // the FAT are reserved.
    if (  ClusterCount > 0x0FFFFFFF )
        {
        free(pFAT32BootSect);
        free(pFAT32FsInfo);
        free(pFirstSectOfFat);
        die ( "This drive has more than 2^28 clusters, try to specify a larger cluster size or use the default (i.e. don't use -cXX)\n" );
        }

    // Sanity check - < 64K clusters means that the volume will be misdetected as FAT16
    if ( ClusterCount < 65536 )
        {
        free(pFAT32BootSect);
        free(pFAT32FsInfo);
        free(pFirstSectOfFat);
        die ( "FAT32 must have at least 65536 clusters, try to specify a smaller cluster size or use the default (i.e. don't use -cXX)\n"  );
        }

    // Sanity check, make sure the fat is big enough
    // Convert the cluster count into a Fat sector count, and check the fat size value we calculated
    // earlier is OK.
    FatNeeded = ClusterCount * 4;
    FatNeeded += (BytesPerSect-1);
    FatNeeded /= BytesPerSect;
    if ( FatNeeded > FatSize )
        {
        free(pFAT32BootSect);
        free(pFAT32FsInfo);
        free(pFirstSectOfFat);
        die ( "This drive is too big for this version of fat32format, check for an upgrade\n" );
        }

    // Now we're commited - print some info first
    //KdPrint (( "SwapFs: Size : %gGB %u sectors\n", (double) (piDrive.PartitionLength.QuadPart / (1000*1000*1000)), TotalSectors ));
    KdPrint (( "SwapFs: %d Bytes Per Sector, Cluster size %d bytes\n", BytesPerSect, SectorsPerCluster*BytesPerSect ));
    KdPrint (( "SwapFs: Volume ID is %x:%x\n", VolumeId>>16, VolumeId&0xffff ));
    KdPrint (( "SwapFs: %d Reserved Sectors, %d Sectors per FAT, %d fats\n", ReservedSectCount, FatSize, NumFATs ));

    KdPrint (( "SwapFs: %I64u Total clusters\n", ClusterCount ));

    // fix up the FSInfo sector
    pFAT32FsInfo->dFree_Count = (UserAreaSize/SectorsPerCluster)-1;
    pFAT32FsInfo->dNxt_Free = 3; // clusters 0-1 resered, we used cluster 2 for the root dir

    KdPrint (( "SwapFs: %d Free Clusters\n", pFAT32FsInfo->dFree_Count ));
    // Work out the Cluster count

    KdPrint (( "SwapFs: Formatting drive...\n" ));

    // Once zero_sectors has run, any data on the drive is basically lost....

    // First zero out ReservedSect + FatSize * NumFats + SectorsPerCluster
    SystemAreaSize = (ReservedSectCount+(NumFATs*FatSize) + SectorsPerCluster);
    KdPrint (( "SwapFs: Clearing out %d sectors for Reserved sectors, fats and root cluster...\n", SystemAreaSize ));
    zero_sectors( hDevice, 0, BytesPerSect, SystemAreaSize);
    KdPrint (( "SwapFs: Initialising reserved sectors and FATs...\n" ));
    // Now we should write the boot sector and fsinfo twice, once at 0 and once at the backup boot sect position
    for ( i=0; i<2; i++ )
        {
        int SectorStart = (i==0) ? 0 : BackupBootSect;
        write_sect ( hDevice, SectorStart, BytesPerSect, pFAT32BootSect, 1 );
        write_sect ( hDevice, SectorStart+1, BytesPerSect, pFAT32FsInfo, 1 );
        }

    // Write the first fat sector in the right places
    for ( i=0; i<NumFATs; i++ )
        {
        int SectorStart = ReservedSectCount + (i * FatSize );
        write_sect ( hDevice, SectorStart, BytesPerSect, pFirstSectOfFat, 1 );
        }

    root_dir = (struct msdos_dir_entry*) pFirstSectOfFat;
    memset(root_dir, 0, BytesPerSect);
    memcpy(root_dir->name, "Swap    ", 8);
    memcpy(root_dir->ext, "   ", 3);
    root_dir->attr = ATTR_VOLUME;
    write_sect ( hDevice, ReservedSectCount + (NumFATs * FatSize ), BytesPerSect, root_dir, 1 );

    free(pFAT32BootSect);
    free(pFAT32FsInfo);
    free(pFirstSectOfFat);

    KdPrint(("SwapFs: Device size is %uMB having %I64u sectors of %u bytes formated to FAT%u using %I64u clusters of %u sectors.\n",
        (ULONG) (piDrive.PartitionLength.QuadPart / 0x100000),
        qTotalSectors, BytesPerSect, 32, ClusterCount, SectorsPerCluster));

    return STATUS_SUCCESS;
}

#pragma code_seg() // end "INIT"
