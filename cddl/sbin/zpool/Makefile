# $FreeBSD$

.PATH: ${.CURDIR}/../../../cddl/contrib/opensolaris/cmd/zpool \
	${.CURDIR}/../../../sys/cddl/contrib/opensolaris/common/zfs

PROG=	zpool
MAN=	zpool.8
SRCS=	zpool_main.c zpool_vdev.c zpool_iter.c zpool_util.c zfs_comutil.c

WARNS?=	0
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+= -I${.CURDIR}/../../../cddl/compat/opensolaris/include
CFLAGS+= -I${.CURDIR}/../../../cddl/compat/opensolaris/lib/libumem
CFLAGS+= -I${.CURDIR}/../../../sys/cddl/compat/opensolaris
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/head
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/lib/libuutil/common
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/lib/libumem/common
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/lib/libzfs/common
CFLAGS+= -I${.CURDIR}/../../../cddl/contrib/opensolaris/lib/libnvpair
CFLAGS+= -I${.CURDIR}/../../../sys/cddl/contrib/opensolaris/common/zfs
CFLAGS+= -I${.CURDIR}/../../../sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${.CURDIR}/../../../sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+= -I${.CURDIR}/../../../sys/cddl/contrib/opensolaris/uts/common/sys

DPADD=	${LIBAVL} ${LIBZFS} ${LIBGEOM} ${LIBBSDXML} ${LIBSBUF} \
	${LIBM} ${LIBNVPAIR} ${LIBUUTIL} ${LIBUTIL}
LDADD=	-lavl -lzfs -lgeom -lbsdxml -lsbuf \
	-lm -lnvpair -luutil -lutil

.include <bsd.prog.mk>
