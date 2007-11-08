/*-
 * Copyright (c) 1992 Diomidis Spinellis.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Diomidis Spinellis of Imperial College, University of London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)main.c	8.2 (Berkeley) 1/3/94";
#endif

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs.h"
#include "extern.h"

/*
 * Linked list of units (strings and files) to be compiled
 */
struct s_compunit {
	struct s_compunit *next;
	enum e_cut {CU_FILE, CU_STRING} type;
	char *s;			/* Pointer to string or fname */
};

/*
 * Linked list pointer to compilation units and pointer to current
 * next pointer.
 */
static struct s_compunit *script, **cu_nextp = &script;

/*
 * Linked list of files to be processed
 */
struct s_flist {
	char *fname;
	struct s_flist *next;
};

/*
 * Linked list pointer to files and pointer to current
 * next pointer.
 */
static struct s_flist *files, **fl_nextp = &files;

FILE *infile;			/* Current input file */
FILE *outfile;			/* Current output file */

int aflag, eflag, nflag;
int rflags = 0;
static int rval;		/* Exit status */

static int ispan;		/* Whether inplace editing spans across files */

/*
 * Current file and line number; line numbers restart across compilation
 * units, but span across input files.  The latter is optional if editing
 * in place.
 */
const char *fname;		/* File name. */
const char *outfname;		/* Output file name */
static char oldfname[PATH_MAX];	/* Old file name (for in-place editing) */
static char tmpfname[PATH_MAX];	/* Temporary file name (for in-place editing) */
static const char *inplace;	/* Inplace edit file extension. */
u_long linenum;

static void add_compunit(enum e_cut, char *);
static void add_file(char *);
static void usage(void);

int
main(int argc, char *argv[])
{
	int c, fflag;
	char *temp_arg;

	(void) setlocale(LC_ALL, "");

	fflag = 0;
	inplace = NULL;

	while ((c = getopt(argc, argv, "EI:ae:f:i:ln")) != -1)
		switch (c) {
		case 'E':
			rflags = REG_EXTENDED;
			break;
		case 'I':
			inplace = optarg;
			ispan = 1;	/* span across input files */
			break;
		case 'a':
			aflag = 1;
			break;
		case 'e':
			eflag = 1;
			if ((temp_arg = malloc(strlen(optarg) + 2)) == NULL)
				err(1, "malloc");
			strcpy(temp_arg, optarg);
			strcat(temp_arg, "\n");
			add_compunit(CU_STRING, temp_arg);
			break;
		case 'f':
			fflag = 1;
			add_compunit(CU_FILE, optarg);
			break;
		case 'i':
			inplace = optarg;
			ispan = 0;	/* don't span across input files */
			break;
		case 'l':
			if(setlinebuf(stdout) != 0)
				warnx("setlinebuf() failed");
			break;
		case 'n':
			nflag = 1;
			break;
		default:
		case '?':
			usage();
		}
	argc -= optind;
	argv += optind;

	/* First usage case; script is the first arg */
	if (!eflag && !fflag && *argv) {
		add_compunit(CU_STRING, *argv);
		argv++;
	}

	compile();

	/* Continue with first and start second usage */
	if (*argv)
		for (; *argv; argv++)
			add_file(*argv);
	else
		add_file(NULL);
	process();
	cfclose(prog, NULL);
	if (fclose(stdout))
		err(1, "stdout");
	exit(rval);
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n",
		"usage: sed script [-Ealn] [-i extension] [file ...]",
		"       sed [-Ealn] [-i extension] [-e script] ... [-f script_file] ... [file ...]");
	exit(1);
}

/*
 * Like fgets, but go through the chain of compilation units chaining them
 * together.  Empty strings and files are ignored.
 */
char *
cu_fgets(char *buf, int n, int *more)
{
	static enum {ST_EOF, ST_FILE, ST_STRING} state = ST_EOF;
	static FILE *f;		/* Current open file */
	static char *s;		/* Current pointer inside string */
	static char string_ident[30];
	char *p;

again:
	switch (state) {
	case ST_EOF:
		if (script == NULL) {
			if (more != NULL)
				*more = 0;
			return (NULL);
		}
		linenum = 0;
		switch (script->type) {
		case CU_FILE:
			if ((f = fopen(script->s, "r")) == NULL)
				err(1, "%s", script->s);
			fname = script->s;
			state = ST_FILE;
			goto again;
		case CU_STRING:
			if ((snprintf(string_ident,
			    sizeof(string_ident), "\"%s\"", script->s)) >=
			    sizeof(string_ident) - 1)
				(void)strcpy(string_ident +
				    sizeof(string_ident) - 6, " ...\"");
			fname = string_ident;
			s = script->s;
			state = ST_STRING;
			goto again;
		}
	case ST_FILE:
		if ((p = fgets(buf, n, f)) != NULL) {
			linenum++;
			if (linenum == 1 && buf[0] == '#' && buf[1] == 'n')
				nflag = 1;
			if (more != NULL)
				*more = !feof(f);
			return (p);
		}
		script = script->next;
		(void)fclose(f);
		state = ST_EOF;
		goto again;
	case ST_STRING:
		if (linenum == 0 && s[0] == '#' && s[1] == 'n')
			nflag = 1;
		p = buf;
		for (;;) {
			if (n-- <= 1) {
				*p = '\0';
				linenum++;
				if (more != NULL)
					*more = 1;
				return (buf);
			}
			switch (*s) {
			case '\0':
				state = ST_EOF;
				if (s == script->s) {
					script = script->next;
					goto again;
				} else {
					script = script->next;
					*p = '\0';
					linenum++;
					if (more != NULL)
						*more = 0;
					return (buf);
				}
			case '\n':
				*p++ = '\n';
				*p = '\0';
				s++;
				linenum++;
				if (more != NULL)
					*more = 0;
				return (buf);
			default:
				*p++ = *s++;
			}
		}
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Like fgets, but go through the list of files chaining them together.
 * Set len to the length of the line.
 */
int
mf_fgets(SPACE *sp, enum e_spflag spflag)
{
	struct stat sb;
	size_t len;
	char *p;
	int c;
	static int firstfile;

	if (infile == NULL) {
		/* stdin? */
		if (files->fname == NULL) {
			if (inplace != NULL)
				errx(1, "-I or -i may not be used with stdin");
			infile = stdin;
			fname = "stdin";
			outfile = stdout;
			outfname = "stdout";
		}
		firstfile = 1;
	}

	for (;;) {
		if (infile != NULL && (c = getc(infile)) != EOF) {
			(void)ungetc(c, infile);
			break;
		}
		/* If we are here then either eof or no files are open yet */
		if (infile == stdin) {
			sp->len = 0;
			return (0);
		}
		if (infile != NULL) {
			fclose(infile);
			if (*oldfname != '\0') {
				if (rename(fname, oldfname) != 0) {
					warn("rename()");
					unlink(tmpfname);
					exit(1);
				}
				*oldfname = '\0';
			}
			if (*tmpfname != '\0') {
				if (outfile != NULL && outfile != stdout)
					fclose(outfile);
				outfile = NULL;
				rename(tmpfname, fname);
				*tmpfname = '\0';
			}
			outfname = NULL;
		}
		if (firstfile == 0)
			files = files->next;
		else
			firstfile = 0;
		if (files == NULL) {
			sp->len = 0;
			return (0);
		}
		fname = files->fname;
		if (inplace != NULL) {
			if (lstat(fname, &sb) != 0)
				err(1, "%s", fname);
			if (!(sb.st_mode & S_IFREG))
				errx(1, "%s: %s %s", fname,
				    "in-place editing only",
				    "works for regular files");
			if (*inplace != '\0') {
				strlcpy(oldfname, fname,
				    sizeof(oldfname));
				len = strlcat(oldfname, inplace,
				    sizeof(oldfname));
				if (len > sizeof(oldfname))
					errx(1, "%s: name too long", fname);
			}
			len = snprintf(tmpfname, sizeof(tmpfname),
			    "%s/.!%ld!%s", dirname(fname), (long)getpid(),
			    basename(fname));
			if (len >= sizeof(tmpfname))
				errx(1, "%s: name too long", fname);
			unlink(tmpfname);
			if ((outfile = fopen(tmpfname, "w")) == NULL)
				err(1, "%s", fname);
			fchown(fileno(outfile), sb.st_uid, sb.st_gid);
			fchmod(fileno(outfile), sb.st_mode & ALLPERMS);
			outfname = tmpfname;
			if (!ispan) {
				linenum = 0;
				resetstate();
			}
		} else {
			outfile = stdout;
			outfname = "stdout";
		}
		if ((infile = fopen(fname, "r")) == NULL) {
			warn("%s", fname);
			rval = 1;
			continue;
		}
	}
	/*
	 * We are here only when infile is open and we still have something
	 * to read from it.
	 *
	 * Use fgetln so that we can handle essentially infinite input data.
	 * Can't use the pointer into the stdio buffer as the process space
	 * because the ungetc() can cause it to move.
	 */
	p = fgetln(infile, &len);
	if (ferror(infile))
		errx(1, "%s: %s", fname, strerror(errno ? errno : EIO));
	if (len != 0 && p[len - 1] == '\n')
		len--;
	cspace(sp, p, len, spflag);

	linenum++;

	return (1);
}

/*
 * Add a compilation unit to the linked list
 */
static void
add_compunit(enum e_cut type, char *s)
{
	struct s_compunit *cu;

	if ((cu = malloc(sizeof(struct s_compunit))) == NULL)
		err(1, "malloc");
	cu->type = type;
	cu->s = s;
	cu->next = NULL;
	*cu_nextp = cu;
	cu_nextp = &cu->next;
}

/*
 * Add a file to the linked list
 */
static void
add_file(char *s)
{
	struct s_flist *fp;

	if ((fp = malloc(sizeof(struct s_flist))) == NULL)
		err(1, "malloc");
	fp->next = NULL;
	*fl_nextp = fp;
	fp->fname = s;
	fl_nextp = &fp->next;
}

int
lastline(void)
{
	int ch;

	if (files->next != NULL && (inplace == NULL || ispan))
		return (0);
	if ((ch = getc(infile)) == EOF)
		return (1);
	ungetc(ch, infile);
	return (0);
}
