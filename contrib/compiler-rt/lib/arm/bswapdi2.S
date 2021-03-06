//===------- bswapdi2 - Implement bswapdi2 --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// extern uint64_t __bswapdi2(uint64_t);
//
// Reverse all the bytes in a 64-bit integer.
//
.align 2
DEFINE_COMPILERRT_FUNCTION(__bswapdi2)
#if __ARM_ARCH_5TEJ__ || __ARM_ARCH_4T__
    // before armv6 does not have "rev" instruction
    // r2 = rev(r0)
    eor r2, r0, r0, ror #16
    bic r2, r2, #0xff0000
    mov r2, r2, lsr #8
    eor r2, r2, r0, ror #8
    // r0 = rev(r1)
    eor r0, r1, r1, ror #16
    bic r0, r0, #0xff0000
    mov r0, r0, lsr #8
    eor r0, r0, r1, ror #8
#else
    rev r2, r0  // r2 = rev(r0)
    rev r0, r1  // r0 = rev(r1)
#endif
    mov r1, r2  // r1 = r2 = rev(r0)
    bx  lr
