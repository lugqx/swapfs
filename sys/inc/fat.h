#ifndef MKDOSFS_H
#define MKDOSFS_H

/* The following is a subset of mkdosfs.c (0.3b) */

/*
   Filename:     mkdosfs.c
   Version:      0.3b (Yggdrasil)
   Author:       Dave Hudson
   Started:      24th August 1994
   Last Updated: 5th May 1995
   Updated by:   H. Peter Anvin <hpa@yggdrasil.com>
   Target O/S:   Linux (1.x)

   Description: Utility to allow an MS-DOS filesystem to be created
   under Linux.  A lot of the basic structure of this program has been
   borrowed from Remy Card's "mke2fs" code.

   As far as possible the aim here is to make the "mkdosfs" command
   look almost identical to the other Linux filesystem make utilties,
   eg bad blocks are still specified as blocks, not sectors, but when
   it comes down to it, DOS is tied to the idea of a sector (512 bytes
   as a rule), and not the block.  For example the boot block does not
   occupy a full cluster.

   Copying:     Copyright 1993, 1994 David Hudson (dave@humbug.demon.co.uk)

   Portions copyright 1992, 1993 Remy Card (card@masi.ibp.fr)
   and 1991 Linus Torvalds (torvalds@klaava.helsinki.fi)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */


#define ATTR_VOLUME  8			/* volume label */

#define MSDOS_EXT_SIGN 0x29		/* extended boot sector signature */
#define MSDOS_FAT12_SIGN "FAT12   "	/* FAT12 filesystem signature */
#define MSDOS_FAT16_SIGN "FAT16   "	/* FAT16 filesystem signature */

#define BOOT_SIGN 0xAA55		/* Boot sector magic number */

struct msdos_boot_sector
{
  unsigned char boot_jump[3];	/* Boot strap short or near jump */
  char system_id[8];			/* Name - can be used to special case
								   partition manager volumes */
  unsigned char sector_size[2];	/* bytes per logical sector */
  unsigned char cluster_size;	/* sectors/cluster */
  unsigned short reserved;		/* reserved sectors */
  unsigned char fats;			/* number of FATs */
  unsigned char dir_entries[2];	/* root directory entries */
  unsigned char sectors[2];		/* number of sectors */
  unsigned char media;			/* media code (unused) */
  unsigned short fat_length;	/* sectors/FAT */
  unsigned short secs_track;	/* sectors per track */
  unsigned short heads;			/* number of heads */
  unsigned long hidden;			/* hidden sectors (unused) */
  unsigned long total_sect;		/* number of sectors (if sectors == 0) */
  unsigned char drive_number;	/* BIOS drive number */
  unsigned char RESERVED;		/* Unused */
  unsigned char ext_boot_sign;	/* 0x29 if fields below exist (DOS 3.3+) */
  unsigned char volume_id[4];	/* Volume ID number */
  char volume_label[11];		/* Volume label */
  char fs_type[8];				/* Typically FAT12 or FAT16 */
  unsigned char boot_code[448];	/* Boot code (or message) */
  unsigned short boot_sign;		/* 0xAA55 */
};

struct msdos_dir_entry
  {
    char name[8], ext[3];		/* name and extension */
    unsigned char attr;			/* attribute bits */
    char unused[10];
    unsigned short time, date, start;	/* time, date and first cluster */
    unsigned long size;			/* file size (in bytes) */
  };

#endif /* MKDOSFS_H */
