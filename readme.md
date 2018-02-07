## MECAFF - Multiline External Console And Fullscreen Facility

MECAFF is a set of programs for **VM/370-R6** mainframe operating system
running under the Hercules emulator, providing fullscreen applications for
3270 terminal emulators.

The MECAFF tools for CMS (the standard OS for users of VM/370) mainly
consist of:

- `EE` : fullscreen editor

- `FSLIST`, `XLIST`, `XXLIST` : various fullscreen file lister

- `FSVIEW` : fullscreen file viewer

- `FSHELP` : fullscreen help viewer

- `IND$FILE` : file transfer utility

The files in this repository will probably be interesting only for users
of VM/370-R6 SixPack 1.2 (or older), as this version originally did not
provide fullscreen support for the console terminal of a VM. These users
will however probably already know the MECAFF tools, as they were published
first in the files area of the Yahoo H390-VM discussion group, the main exchange
platform for VM/370 users.

This repository is an alternative location for the tools, providing all releases
of MECAFF (the group has only the newest version).


#### History overview

The first steps in development of the MECAFF tool suite started in January 2011. At
this time the control program (`CP`) of VM/370-R6 did not allow to share the access
to the 3270 terminal connected to the VM of a logged on user. This effectively prevented
a CMS program to take control over the 3270 screen and provide a fullscreen interaction
e.g. for an editor. (for insiders: the DIAG-X'58' interface of the original VM/370-R6 did
not support the necessary fullscreen subfunctions as they were added in later versions
of the OS)

To circumvent this restriction, an external console program was written in Java, which
acts as a kind of proxy between the the 3270 terminal and the user VM and allows a CMS
program to provide fullscreen functionality through the specialized MECAFF API. Up to version
1.0.0 of MECAFF, this Java program was required to use the MECAFF fullscreen tools. 

In 2013 Bob Polmanter published his modifications to the CP kernel which added the
fullscreen functionality to DIAG-X'58', providing the standard interface of the more
modern versions of VM/xx.

This allowed to develop fullscreen programs without the intermediary proxy program.
The external Java program can however still be used, as it provides a more comfortable
user console compared to the original CP console, with scrolling through the
output history, extended function key support etc.

The latest [release notes](./publications/v1.2.5/MECAFF-tools-ReleaseNotes-1.2.5.pdf)
document lists the evolution of the MECAFF suite in more detail.

#### Status

The current version is 1.2.5 (see below). The versions before 1.0.0 were
considered beta-level and were therefore published as binary only package with the
programs, documentation and the public API, but without source code. Starting
with version 1.0.0, the packages also contain the source code both for the
C programs under CMS and the external Java console extension program. The sources
for version 1.2.5 are directly available in this repository as well as in the
ZIP-file for version 1.2.5, the sources for the older versions down to 1.0.0
are contained only in the respective delivery ZIP-file.

Rebuilding the fullscreen tools for CMS requires working with VM/370-R6 SixPack 1.2,
as this version has the GCC compiler and the native C-runtime library necessary to
compile and link the programs. For the optional Java console extension program, any
contemporary Eclipse version should be sufficient for rebuilding. 

#### License

The MECAFF binaries and sources are released to the public domain.

#### Repository subdirectories 

The directories `cms` and `java` contain the sources for the current version 1.2.5
of MECAFF:

- the directory [cms](./cms) contains the files to build the MECAFF tools for the mainframe
side, besides the `H` and `C` files these are the `EXEC` scripts for building and running
the `MODULE` files as well as some support files.

- the directory [java](./java) contains the Eclipse project for the Java console extension program.

The directory [publications](./publications) has a subdirectory for each release of MECAFF uploaded
to the files area of the Yahoo H390-VM discussion group.    
These are the following versions published at the specified date:

- [0.9.0](./publications/v0.9.0) : 2011-07-29

- [0.9.1](./publications/v0.9.1) : 2011-08-21

- [0.9.3](./publications/v0.9.3) : 2011-10-15

- [0.9.4](./publications/v0.9.4) : 2011-11-29

- [0.9.5](./publications/v0.9.5) : 2011-12-05

- [0.9.6](./publications/v0.9.6) : 2012-01-21

- [0.9.7](./publications/v0.9.7) : 2012-03-10

- [0.9.8](./publications/v0.9.8) : 2012-04-21

- [1.0.0](./publications/v1.0.0) : 2012-05-25

- [1.1.0](./publications/v1.1.0) : 2013-02-21

- [1.2.0](./publications/v1.2.0) : 2013-04-25

- [1.2.5](./publications/v1.2.5) : 2013-07-18

Each subdirectory contains the original ZIP-files published for the
corresponding release, as well as the documentation PDF file(s) contained
in this ZIP-file.    
The files to be installed on the emulated mainframe are delivered as AWS
tape files, which can be mounted by the Hercules emulator, the tapes were
written with the CMS `TAPE` command. 