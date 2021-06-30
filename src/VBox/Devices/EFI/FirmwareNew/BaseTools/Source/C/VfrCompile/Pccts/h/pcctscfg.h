#ifndef PCCTS_CONFIG_H
#define PCCTS_CONFIG_H
/*
 * pcctscfg.h (formerly config.h) (for ANTLR, DLG, and SORCERER)
 *
 * This is a simple configuration file that doesn't have config stuff
 * in it, but it's a start.
 *
 * SOFTWARE RIGHTS
 *
 * We reserve no LEGAL rights to the Purdue Compiler Construction Tool
 * Set (PCCTS) -- PCCTS is in the public domain.  An individual or
 * company may do whatever they wish with source code distributed with
 * PCCTS or the code generated by PCCTS, including the incorporation of
 * PCCTS, or its output, into commerical software.
 *
 * We encourage users to develop software with PCCTS.  However, we do ask
 * that credit is given to us for developing PCCTS.  By "credit",
 * we mean that if you incorporate our source code into one of your
 * programs (commercial product, research project, or otherwise) that you
 * acknowledge this fact somewhere in the documentation, research report,
 * etc...  If you like PCCTS and have developed a nice tool with the
 * output, please mention that you developed it using PCCTS.  In
 * addition, we ask that this header remain intact in our source code.
 * As long as these guidelines are kept, we expect to continue enhancing
 * this system and expect to make other tools available as they are
 * completed.
 *
 * Used by PCCTS 1.33 (SORCERER 1.00B11 and up)
 * Terence Parr
 * Parr Research Corporation
 * with Purdue University and AHPCRC, University of Minnesota
 * 1989-2000
 */

/* This file knows about the following ``environments''
	UNIX    (default)
	DOS     (use #define PC)
	MAC     (use #define MPW; has a few things for THINK C, Metrowerks)
    MS/C++  (MR14 Microsoft Visual C++ environment uses symbol _MSC_VER)

 */

/* should test __STDC__ for 1, but some compilers don't set value, just def */

#ifndef __USE_PROTOS
#ifdef __STDC__
#define __USE_PROTOS
#endif
#ifdef __cplusplus
#define __USE_PROTOS
#endif
#endif

#ifdef PCCTS_USE_NAMESPACE_STD
#define PCCTS_NAMESPACE_STD     namespace std {}; using namespace std;
#else
#define PCCTS_NAMESPACE_STD
#endif

#include "pccts_stdio.h"
#include "pccts_stdlib.h"

/* largest file name size */

#ifdef _MAX_PATH
#define MaxFileName		_MAX_PATH /* MR9 RJV: MAX_PATH defined in stdlib.h (MSVC++ 5.0) */
#else
#define MaxFileName		300
#endif

/*
*  Define PC32 if in a 32-bit PC environment (e.g. extended DOS or Win32).
*  The macros tested here are defined by Watcom, Microsoft, Borland,
*  and djgpp, respectively, when they are used as 32-bit compilers.
*  Users of these compilers *must* be sure to define PC in their
*  makefiles for this to work correctly.
*/
#ifdef PC
# if (defined(__WATCOMC__) || defined(_WIN32) || defined(__WIN32__) || \
   defined(__GNUC__) || defined(__GNUG__))
#     ifndef PC32
#        define PC32
#     endif
#  endif
#endif

/* MR1  10-Apr-97  Default for PC is short file names			            */
/* MR1		   Default for non-PC is long file names                		*/
/* MR1		   Can override via command line option LONGFILENAMES           */

#ifndef LONGFILENAMES
#ifndef PC
#define LONGFILENAMES
#endif
#endif

#ifndef LONGFILENAMES
#define ATOKEN_H			"AToken.h"
#define ATOKPTR_H			"ATokPtr.h"
#define ATOKPTR_IMPL_H		"ATokPtrIm.h"
#define ATOKENBUFFER_H		"ATokBuf.h"
#define ATOKENBUFFER_C      "ATokBuf.cpp"
#define ATOKENSTREAM_H		"ATokStr.h"
#define APARSER_H			"AParser.h"
#define APARSER_C           "AParser.cpp"
#define ASTBASE_H			"ASTBase.h"
#define ASTBASE_C           "ASTBase.cpp"
#define PCCTSAST_C          "PCCTSAST.cpp"
#define LIST_C              "List.cpp"
#define DLEXERBASE_H		"DLexBase.h"
#define DLEXERBASE_C        "DLexBase.cpp"
#define DLEXER_H            "DLexer.h"
#define STREESUPPORT_C		"STreeSup.C"
#else
#define ATOKEN_H			"AToken.h"
#define ATOKPTR_H			"ATokPtr.h"
#define ATOKPTR_IMPL_H		"ATokPtrImpl.h"
#define ATOKENBUFFER_H		"ATokenBuffer.h"
#define ATOKENBUFFER_C		"ATokenBuffer.cpp"
#define ATOKENSTREAM_H		"ATokenStream.h"
#define APARSER_H			"AParser.h"
#define APARSER_C			"AParser.cpp"
#define ASTBASE_H			"ASTBase.h"
#define ASTBASE_C		    "ASTBase.cpp"
#define PCCTSAST_C			"PCCTSAST.cpp"
#define LIST_C				"List.cpp"
#define DLEXERBASE_H		"DLexerBase.h"
#define DLEXERBASE_C		"DLexerBase.cpp"
#define DLEXER_H			"DLexer.h"
#define STREESUPPORT_C		"STreeSupport.cpp"
#endif

/* SORCERER Stuff */

/* MR8 6-Aug-97     Change from ifdef PC to ifndef LONGFILENAMES            */

#ifndef LONGFILENAMES
#define STPARSER_H			"STreePar.h"
#define STPARSER_C			"STreePar.C"
#else
#define STPARSER_H			"STreeParser.h"
#define STPARSER_C			"STreeParser.cpp"
#endif

#ifdef MPW
#define CPP_FILE_SUFFIX		".cp"
#define CPP_FILE_SUFFIX_NO_DOT	"cp"
#define OBJ_FILE_SUFFIX		".o"
#else
#ifdef PC
#define CPP_FILE_SUFFIX		".cpp"
#define CPP_FILE_SUFFIX_NO_DOT	"cpp"
#define OBJ_FILE_SUFFIX		".obj"
#else
#ifdef __VMS
#define CPP_FILE_SUFFIX		".cpp"
#define CPP_FILE_SUFFIX_NO_DOT	"cpp"
#define OBJ_FILE_SUFFIX		".obj"
#else
#define CPP_FILE_SUFFIX		".cpp"
#define CPP_FILE_SUFFIX_NO_DOT	"cpp"
#define OBJ_FILE_SUFFIX		".o"
#endif
#endif
#endif

/* User may redefine how line information looks */     /* make it #line MR7 */
/* MR21 Use #ifndef */

#ifndef LineInfoFormatStr
#define LineInfoFormatStr "#line %d \"%s\"\n"
#endif

#ifdef MPW	                    /* Macintosh Programmer's Workshop */
#define ErrHdr "File \"%s\"; Line %d #"
#else
#ifdef _MSC_VER                 /* MR14 Microsoft Visual C++ environment */
#define ErrHdr "%s(%d) :"
#else
#define ErrHdr "%s, line %d:"   /* default */
#endif
#endif

/* must assume old K&R cpp here, can't use #if defined(..)... */

#ifdef MPW
#define TopDirectory	":"
#define DirectorySymbol	":"
#define OutputDirectoryOption "Directory where all output files should go (default=\":\")"
#else
#ifdef PC
#define TopDirectory	"."
#define DirectorySymbol	"\\"
#define OutputDirectoryOption "Directory where all output files should go (default=\".\")"
#else
#ifdef __VMS
#define TopDirectory  "[000000]"
#define DirectorySymbol       "]"
#define OutputDirectoryOption "Directory where all output files should go (default=\"[]\")"
#else
#define TopDirectory	"."
#define DirectorySymbol	"/"
#define OutputDirectoryOption "Directory where all output files should go (default=\".\")"
#endif
#endif
#endif

#ifdef MPW

/* Make sure we have prototypes for all functions under MPW */

#include "pccts_string.h"
#include "pccts_stdlib.h"

/* MR6 2-Jun-97	Fixes false dependency caused by VC++ #include scanner	*/
/* MR6		   Reported by Brad Schick (schick@interaccess.com)	*/
#define	MPW_CursorCtl_Header <CursorCtl.h>
#include MPW_CursorCtl_Header
#ifdef __cplusplus
extern "C" {
#endif
extern void fsetfileinfo (const char *filename, unsigned long newcreator, unsigned long newtype);
#ifdef __cplusplus
}
#endif

/* File creators for various popular development environments */

#define MAC_FILE_CREATOR 'MPS '   /* MPW Text files */
#if 0
#define MAC_FILE_CREATOR 'KAHL'   /* THINK C/Symantec C++ Text files */
#endif
#if 0
#define MAC_FILE_CREATOR 'CWIE'   /* Metrowerks C/C++ Text files */
#endif

#endif

#ifdef MPW
#define DAWDLE	SpinCursor(1)
#else
#define DAWDLE
#endif

#ifdef MPW
#define SPECIAL_INITS
#define SPECIAL_FOPEN
#endif

#ifdef MPW
#ifdef __cplusplus
inline
#else
static
#endif
void special_inits()
{
  InitCursorCtl((acurHandle) 0);
}
#endif

#ifdef MPW
#ifdef __cplusplus
inline
#else
static
#endif
void special_fopen_actions(char * s)
{
  fsetfileinfo (s, MAC_FILE_CREATOR, 'TEXT');
}
#endif

/* Define usable bits for set.c stuff */
#define BytesPerWord	sizeof(unsigned)
#define	WORDSIZE		(sizeof(unsigned)*8)
#define LogWordSize     (WORDSIZE==16?4:5)

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#if defined(VAXC) || defined(__VMS)
#include <ssdef.h>
#define PCCTS_EXIT_SUCCESS 1
#define PCCTS_EXIT_FAILURE SS$_ABORT
#define zzDIE		return SS$_ABORT;
#define zzDONE	return 1;

#else /* !VAXC and !__VMS */

#define PCCTS_EXIT_SUCCESS 0
#define PCCTS_EXIT_FAILURE 1
#define zzDIE		return 1;
#define zzDONE	return 0;

#endif

#ifdef USER_ZZMODE_STACK
# ifndef ZZSTACK_MAX_MODE
#  define  ZZSTACK_MAX_MODE 32
# endif
# define  ZZMAXSTK (ZZSTACK_MAX_MODE * 2)
#endif

#ifndef DllExportPCCTS
#define DllExportPCCTS
#endif

#ifdef PC
#ifndef PCCTS_CASE_INSENSITIVE_FILE_NAME
#define PCCTS_CASE_INSENSITIVE_FILE_NAME
#endif
#endif

#ifdef PC32
#ifndef PCCTS_CASE_INSENSITIVE_FILE_NAME
#define PCCTS_CASE_INSENSITIVE_FILE_NAME
#endif
#endif

#ifdef __VMS
#ifndef PCCTS_CASE_INSENSITIVE_FILE_NAME
#define PCCTS_CASE_INSENSITIVE_FILE_NAME
#endif
#endif

#ifdef __USE_PROTOS
#ifndef PCCTS_USE_STDARG
#define PCCTS_USE_STDARG
#endif
#endif

#ifdef __STDC__
#ifndef PCCTS_USE_STDARG
#define PCCTS_USE_STDARG
#endif
#endif

#ifdef __cplusplus
#ifndef PCCTS_USE_STDARG
#define PCCTS_USE_STDARG
#endif
#endif

#ifdef _MSC_VER
/*Turn off the warnings for:
  unreferenced inline/local function has been removed
*/
#pragma warning(disable : 4514)
/* function not expanded */
#pragma warning(disable : 4710)
#endif

#endif
