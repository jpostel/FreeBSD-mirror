/* $FreeBSD$ */
/* include/llvm/Config/llvm-config.h.  Generated from llvm-config.h.in by configure.  */
/*===-- llvm/config/llvm-config.h - llvm configure variable -------*- C -*-===*/
/*                                                                            */
/*                     The LLVM Compiler Infrastructure                       */
/*                                                                            */
/* This file is distributed under the University of Illinois Open Source      */
/* License. See LICENSE.TXT for details.                                      */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

/* This file enumerates all of the llvm variables from configure so that
   they can be in exported headers and won't override package specific
   directives.  This is a C file so we can include it in the llvm-c headers.  */

/* To avoid multiple inclusions of these variables when we include the exported
   headers and config.h, conditionally include these.  */
/* TODO: This is a bit of a hack.  */
#ifndef CONFIG_H

/* Build multithreading support into LLVM */
/* #undef LLVM_MULTITHREADED */

/* LLVM architecture name for the native architecture, if available */
#define LLVM_NATIVE_ARCH X86Target

/* Short LLVM architecture name for the native architecture, if available */
#define LLVM_NATIVE_ARCHNAME X86

/* Define if this is Unixish platform */
#define LLVM_ON_UNIX 1

/* Define if this is Win32ish platform */
/* #undef LLVM_ON_WIN32 */

/* Define to path to circo program if found or 'echo circo' otherwise */
/* #undef LLVM_PATH_CIRCO */

/* Define to path to dot program if found or 'echo dot' otherwise */
/* #undef LLVM_PATH_DOT */

/* Define to path to dotty program if found or 'echo dotty' otherwise */
/* #undef LLVM_PATH_DOTTY */

/* Define to path to fdp program if found or 'echo fdp' otherwise */
/* #undef LLVM_PATH_FDP */

/* Define to path to Graphviz program if found or 'echo Graphviz' otherwise */
/* #undef LLVM_PATH_GRAPHVIZ */

/* Define to path to gv program if found or 'echo gv' otherwise */
/* #undef LLVM_PATH_GV */

/* Define to path to neato program if found or 'echo neato' otherwise */
/* #undef LLVM_PATH_NEATO */

/* Define to path to twopi program if found or 'echo twopi' otherwise */
/* #undef LLVM_PATH_TWOPI */

/* Installation prefix directory */
#define LLVM_PREFIX "/usr"

#endif
