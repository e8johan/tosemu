TOSEMU
An emulated environment to TOS applications.
Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>

[![Build Status](https://travis-ci.org/e8johan/tosemu.svg?branch=master)](https://travis-ci.org/e8johan/tosemu)

Introduction
============

TOSEMU aims to provide an emulated environment for executing TOS applications. 
Instead of emulating a complete TOS-compatible machine, operating system calls 
are intercepted and emulated on the host platform. The end result will be TOS 
applications running as an integrated part of the host system.

TOSEMU can be seen as a mix of 68kemu, which allows 68k applications to be 
executed in a TOS environment on another CPU platform, and wine, which lets 
Windows applications execute using a system API translation laer on the CPU 
platform they where meant for. TOSEMU executes 68k applications on non-68k 
CPUs, and provides an OS wrapper layer, translating TOS calls to system calls 
native to the host operating system.

I use LLM technology to speed things along from time to time. Feel free to join
in by adding functionality, reporting bugs or helping out in any other way.



Building
========

TOSEMU is self contained for now, so a simple `make` should do it. The resulting 
binary can be found in the bin directory.

The `make clean` target produces a clean source tree.



Usage
=====

TOSEMU takes a single command line argument, the location of a TOS application. 
It is also possible to use TOSEMU with the binfmt support in the Linux kernel. I
 use the following line to enable this:

  `echo ':tos:M::\x60\x1a:\xff\xff:/path/to/binary/tosemu:' | sudo tee /proc/sys/fs/binfmt_misc/register`

This will allow you to execute TOS binaries as if they where native.

The application is handed the environment tosemu was started with, so a
variable an application looks for can be set from the host shell. Lattice C's
compiler for instance finds its header files through `INCLUDE`.

A TOS command line lives in the basepage and holds no more than 126 characters.
Arguments beyond that are dropped, which is all a TOS program can be handed.
Applications that need more, such as linkers, read a control file instead.



Road Map
========

The first stage will be to get a very basic TOS application to execute using 
TOSEMU. Having achieved this, more and more complex apps will be supported by 
extending the available system calls.

When basic applications are useable, a server/client architecture enabling 
intra-app communication as well as desktop accessory applications will be 
implemented.



Tests
=====

The tests subdirectory contains test applications used during the development 
of tosemu. The tests are compiled with the m68k-atari-mint cross-tools built by
 Vincent Rivière. Please visit the following web site for more information:

  http://vincent.riviere.free.fr/soft/m68k-atari-mint/

To build the tests, simply run make tests, this will result in a set of binaries
named test-* in the tests sub-directory. Run `make check` to build and run them.

Self-hosted tests
-----------------

The `tests/devpac` subdirectory contains the same test cases written in the
syntax of HiSoft Devpac 3.10's Gen assembler. These are not cross assembled on
the host - they are built by running `GEN.TTP` itself inside tosemu, so the test
run exercises a real, non-trivial TOS application as well as the binaries it
produces.

These tests need a Devpac 3.10 installation. Point `TOS_ROOT` at the directory
containing `devpac31`, which defaults to a `tos_root` next to the tosemu source
tree:

  `make devpac-check TOS_ROOT=/path/to/tos_root`

Note that Gen only recognises CR and CR/LF as line terminators, so the sources
in `tests/devpac` are stored with CR/LF endings.



Hacking
=======

Tracing
-------

The `config.h` file contains defines for enabling extremely verbose trace
messages. This is a great tool when debugging a subsystem, e.g. bios or aes.

There is no dependecy to `config.h`, so a clean build is needed for changes to 
take effect.

Unimplemented functions
-----------------------

No documented BIOS or XBIOS call halts the emulator. An application that asks
about hardware tosemu does not have gets the documented answer meaning "this
did not happen", which is not the same as pretending it succeeded: `Flopwr`
reports that the drive is not ready rather than that it wrote something.

Those answers live in the function tables in `bios.c` and `xbios.c` rather than
in a function each. A row carrying `FN_STUB` and a value is the whole of that
call's behaviour, and the trace says which ones an application relied on:

    Stubbed Blitmode (0x40)
    Return from Blitmode: 0 = 0x0

A row carrying `FN_HALT` with no implementation still stops the emulator, as
does any function id that is not in the table at all. Both mean an application
went somewhere nobody has looked at yet.

Where a call has to answer through a pointer rather than in D0, it needs a real
implementation even when the answer is "nothing" - see `Getmpb` in `bios.c`.

Endianess
---------

As the m68k is a big endian architecture, while the current development 
architecture, x86, is little endian, conversion is sometimes needed. However,
in most cases, it is not.

When interacting with the CPU through the `m68k_read_*`, `m68k_write_*`,
`pop_*`, `peek_*`, `push_*`, all values are expected to be in host endianess,
i.e. no conversion is necessary. When interacting directly with the TOS memory,
i.e. manipulating a memory area such as `te->appmem`, endianess is a factor.
Here, the functions `endianize_16` and `endianize_32` help with the conversion.
By always using these methods, it will be possible to run tosemu on host
systems that are either big or little endian.

File names
----------

TOS file systems are case insensitive, and most TOS applications rely on this,
happily asking for `FILE.TXT` when the file is called `file.txt` - Gen for
instance upper cases every include file name. Host file systems usually are not
case insensitive, so `path_from_tos` in `gemdosfile.c` resolves each path
component against the host, accepting a component that only differs in case.
Components without a match are left alone, so `Fcreate` and `Dcreate` still
create names exactly as the application spelled them.

C: is the whole host file system, so a path that starts at the root of the
drive starts at the host root. `Dgetpath` hands out such a path, and an
application that builds a file name from it has to arrive back at the same
file. A `TOS_BASE_PATH` moves that root, and then the drive begins there
instead.

Variable Scope
--------------

In order to provide abstraction and separation of namespaces, different 
subsystems are separated into different code modules. Header files ending with 
`_p.h` are local to such a namespace, i.e. `gemdos_p.h` is local to GEMDOS. 
Depending on the complexity of the subsystems, further subdivision is possible, 
i.e. GEMDOS and XBIOS are split into multiple modules while BIOS is not.

A subsystem that another one has to ask something of gets a second, public
header holding just that much. `drives.h` is the whole of what BIOS may know
about the GEMDOS drive table, which is otherwise private to `gemdosdrive_p.h`.



Licensing
=========

TOSEMU is available under a GPLv2 license. Please refer to the source code and 
the COPYING file for further details.



Additional Licenses
-------------------

TOSEMU depends on other components available under other licenses than GPLv2. 
These are listed below:

The contents of the `Musashi` subdirectory and `m68kconf.h`, derived from 
https://github.com/kstenerud/Musashi, is subject to the following license:

> MUSASHI
> Version 3.4
> 
> A portable Motorola M680x0 processor emulation engine.
> Copyright 1998-2001 Karl Stenerud.  All rights reserved.
> 
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
> 
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
