#	$NetBSD: Makefile,v 1.10 2006/05/12 02:01:15 mrg Exp $
# $FreeBSD$

.include <bsd.own.mk>

PROG=		gzip
MAN=		gzip.1 gzexe.1 zdiff.1 zforce.1 zmore.1 znew.1

DPADD=		${LIBZ}
LDADD=		-lz

.if ${MK_BZIP2_SUPPORT} != "no"
DPADD+=		${LIBBZ2}
LDADD+=		-lbz2
.else
CFLAGS+=	-DNO_BZIP2_SUPPORT
.endif

SCRIPTS=	gzexe zdiff zforce zmore znew

MLINKS+=	gzip.1 gunzip.1 \
		gzip.1 gzcat.1 \
		gzip.1 zcat.1 \
		zdiff.1 zcmp.1

LINKS+=		${BINDIR}/gzip ${BINDIR}/gunzip \
		${BINDIR}/gzip ${BINDIR}/gzcat \
		${BINDIR}/gzip ${BINDIR}/zcat \
		${BINDIR}/zdiff ${BINDIR}/zcmp

.include <bsd.prog.mk>
