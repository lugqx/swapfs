
    This is a disk filter driver for Windows that uses a Linux swap partition
    to provide a temporary storage area formated to the FAT file system.
    Copyright (C) 1999-2020 Bo Brantén.

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

    Windows and Windows NT are either registered trademarks or trademarks of
    Microsoft Corporation in the United States and/or other countries.

    Please send comments, corrections and contributions to bosse@acc.umu.se.

    The most recent version of this program is available from:
    http://www.acc.umu.se/~bosse/

    Revision history:

    3.3 2020-05-27
       The drivers are testsigned, you can allow Windows to load testsigned
       drivers with the command "bcdedit /set testsigning on".

    3.2 2016-01-03
       Support for more than one swap partition.

    3.11 2016-01-03
       Corrected a small error in formating to FAT32 that CHKDSK found.

    3.1 2015-12-15
       Support for GUID Partition Table (GPT) in addition to MBR.

    3.06 2015-10-05
       Improved compatibility with Visual Studio Community 2015 and
       Windows Driver Kit (WDK) 10.

    3.05 2015-09-27
       Small correction of the handling of PNP requests.

    3.04 2015-09-22
       Minor change, can be compiled with Visual Studio Community 2015 and
       Windows Driver Kit (WDK) 10.

    3.03 2015-04-21
       Minor change, can be compiled with both DDK and WDK.

    3.02 2015-04-16
       Small cleanup of debug prints.

    3.01 2015-04-15
       Small update to pass all code analyzes in WDK 7.1.0.

    3.0 2010-07-21
       Support for FAT32 and swap partitions bigger than 4GB.

    2.1 2008-08-02
       Fix for crash on big swap partitions.
       Can be compiled with the WDK.

    2. 2002-08-25
       Support for Plug and Play and Power Management.

    1. 1999-05-02
       Initial release.
