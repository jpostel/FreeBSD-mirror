# $Id: Makefile.am 20466 2007-04-20 08:29:05Z lha $

include $(top_srcdir)/Makefile.am.common

AM_CPPFLAGS += -I$(srcdir)/.. $(INCLUDE_krb4) $(INCLUDE_hcrypto)

libexec_PROGRAMS = telnetd

CHECK_LOCAL = 

telnetd_SOURCES  = telnetd.c state.c termstat.c slc.c sys_term.c \
		   utility.c global.c authenc.c defs.h ext.h telnetd.h

man_MANS = telnetd.8

LDADD = \
	../libtelnet/libtelnet.a \
	$(LIB_krb5) \
	$(LIB_krb4) \
	$(LIB_hcrypto) \
	$(LIB_tgetent) \
	$(LIB_logwtmp) \
	$(LIB_logout) \
	$(LIB_openpty) \
	$(LIB_kdfs) \
	$(LIB_roken)

EXTRA_DIST = $(man_MANS)
