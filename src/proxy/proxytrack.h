/* ------------------------------------------------------------ */
/*
HTTrack Website Copier, Offline Browser for Windows and Unix
Copyright (C) 1998-2016 Xavier Roche and other contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

Please visit our Website: http://www.httrack.com
*/

/* ------------------------------------------------------------ */
/* File: ProxyTrack, httrack cache-based proxy                  */
/* Author: Xavier Roche                                         */
/* ------------------------------------------------------------ */

#ifndef WEBHTTRACK_PROXYTRACK
#define WEBHTTRACK_PROXYTRACK

/* Version */
#define PROXYTRACK_VERSION "0.5"

/* Store manager */
#include "../minizip/mztools.h"
#include "store.h"

#include <sys/stat.h>
#ifndef HTS_DO_NOT_USE_FTIME
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif
#include <sys/timeb.h>
#else
#include <utime.h>
#endif
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stdarg.h>

/* generic */

int proxytrack_main(char *proxyAddr, int proxyPort, char *icpAddr, int icpPort,
                    PT_Indexes index);

/* Spaces: CR,LF,TAB,FF */
#define  is_space(c)      ( ((c)==' ') || ((c)=='\"') || ((c)==10) || ((c)==13) || ((c)==9) || ((c)==12) || ((c)==11) || ((c)=='\'') )
#define  is_realspace(c)  ( ((c)==' ')                || ((c)==10) || ((c)==13) || ((c)==9) || ((c)==12) || ((c)==11)                )
#define  is_taborspace(c) ( ((c)==' ')                                          || ((c)==9)                             )
#define  is_quote(c)      (               ((c)=='\"')                                                    || ((c)=='\'') )
#define  is_retorsep(c)   (                              ((c)==10) || ((c)==13) || ((c)==9)                                          )

/* Static definitions */

HTS_UNUSED static void proxytrack_print_log(const char *severity, const char *format, ...) {
  if (severity != NULL) {
    const int error = errno;
    FILE *const fp = stderr;
    va_list args;

    fprintf(fp, " * %s: ", severity);
    va_start(args, format);
    (void) vfprintf(fp, format, args);
    va_end(args);
    fputs("\n", fp);
    fflush(fp);
    errno = error;
  }
}

#define CRITICAL "critical"
#define WARNING "warning"
#define LOG "log"
#if defined(_DEBUG) || defined(DEBUG)
#define DEBUG "debug"
#else
#define DEBUG NULL
#endif

/* Header for generated pages */
#define PROXYTRACK_COMMENT_HEADER \
	"<!-- Generated by ProxyTrack " PROXYTRACK_VERSION " -->\r\n" \
	"<!-- This is an add-on for HTTrack " HTTRACK_VERSIONID " -->\r\n"

/* See IE "feature" (MSKB Q294807) */
#define DISABLE_IE_FRIENDLY_HTTP_ERROR_MESSAGES											\
	"<!-- Start Disable IE Friendly HTTP Error Messages -->\r\n"			\
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- _-._.--._._-._.--._._-._.--._._-._.--._._-._.--._. -->\r\n" \
	"<!-- End Disable IE Friendly HTTP Error Messages -->\r\n"

HTS_UNUSED static const char *gethomedir(void) {
  const char *home = getenv("HOME");

  if (home)
    return home;
  else
    return ".";
}

HTS_UNUSED static int linput(FILE * fp, char *s, int max) {
  int c;
  int j = 0;

  do {
    c = fgetc(fp);
    if (c != EOF) {
      switch (c) {
      case 13:
        break;                  // sauter CR
      case 10:
        c = -1;
        break;
      case 0:
      case 9:
      case 12:
        break;                  // sauter ces caractères
      default:
        s[j++] = (char) c;
        break;
      }
    }
  } while((c != -1) && (c != EOF) && (j < (max - 1)));
  s[j] = '\0';
  return j;
}

HTS_UNUSED static int link_has_authority(const char *lien) {
  const char *a = lien;

  if (isalpha((const unsigned char) *a)) {
    // Skip scheme?
    while(isalpha((const unsigned char) *a))
      a++;
    if (*a == ':')
      a++;
    else
      return 0;
  }
  if (strncmp(a, "//", 2) == 0)
    return 1;
  return 0;
}

HTS_UNUSED static const char *jump_protocol(const char *source) {
  int p;

  // scheme
  // "Comparisons of scheme names MUST be case-insensitive" (RFC2616)
  if ((p = strfield(source, "http:")))
    source += p;
  else if ((p = strfield(source, "ftp:")))
    source += p;
  else if ((p = strfield(source, "https:")))
    source += p;
  else if ((p = strfield(source, "file:")))
    source += p;
  // net_path
  if (strncmp(source, "//", 2) == 0)
    source += 2;
  return source;
}

HTS_UNUSED static const char *strrchr_limit(const char *s, char c, const char *limit) {
  if (limit == NULL) {
    char *p = strrchr(s, c);

    return p ? (p + 1) : NULL;
  } else {
    char *a = NULL, *p;

    for(;;) {
      p = strchr((a) ? a : s, c);
      if ((p >= limit) || (p == NULL))
        return a;
      a = p + 1;
    }
  }
}

HTS_UNUSED static const char *jump_protocol_and_auth(const char *source) {
  const char *a, *trytofind;

  if (strcmp(source, "file://") == 0)
    return source;
  a = jump_protocol(source);
  trytofind = strrchr_limit(a, '@', strchr(a, '/'));
  return (trytofind != NULL) ? trytofind : a;
}

#ifndef min
#define min(a,b) ((a)>(b)?(b):(a))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
HTS_UNUSED static int linput_trim(FILE * fp, char *s, int max) {
  int rlen = 0;
  char *const ls = (char *) malloc(max + 1);

  s[0] = '\0';
  if (ls) {
    char *a;

    // lire ligne
    rlen = linput(fp, ls, max);
    if (rlen) {
      // sauter espaces et tabs en fin
      while((rlen > 0) && is_realspace(ls[max(rlen - 1, 0)]))
        ls[--rlen] = '\0';
      // sauter espaces en début
      a = ls;
      while((rlen > 0) && ((*a == ' ') || (*a == '\t'))) {
        a++;
        rlen--;
      }
      if (rlen > 0) {
        memcpy(s, a, rlen);     // can copy \0 chars
        s[rlen] = '\0';
      }
    }
    //
    free(ls);
  }
  return rlen;
}

#ifndef S_ISREG
#define S_ISREG(m) ((m) & _S_IFREG)
#endif
HTS_UNUSED static int fexist(char *s) {
  struct stat st;

  memset(&st, 0, sizeof(st));
  if (stat(s, &st) == 0) {
    if (S_ISREG(st.st_mode)) {
      return 1;
    }
  }
  return 0;
}

/* convertir une chaine en temps */
HTS_UNUSED static void set_lowcase(char *s) {
  int i;

  for(i = 0; i < (int) strlen(s); i++)
    if ((s[i] >= 'A') && (s[i] <= 'Z'))
      s[i] += ('a' - 'A');
}
HTS_UNUSED static struct tm *convert_time_rfc822(struct tm *result, const char *s) {
  char months[] = "jan feb mar apr may jun jul aug sep oct nov dec";
  char str[256];
  char *a;

  /* */
  int result_mm = -1;
  int result_dd = -1;
  int result_n1 = -1;
  int result_n2 = -1;
  int result_n3 = -1;
  int result_n4 = -1;

  /* */

  if ((int) strlen(s) > 200)
    return NULL;
  strcpy(str, s);
  set_lowcase(str);
  /* éliminer :,- */
  while((a = strchr(str, '-')))
    *a = ' ';
  while((a = strchr(str, ':')))
    *a = ' ';
  while((a = strchr(str, ',')))
    *a = ' ';
  /* tokeniser */
  a = str;
  while(*a) {
    char *first, *last;
    char tok[256];

    /* découper mot */
    while(*a == ' ')
      a++;                      /* sauter espaces */
    first = a;
    while((*a) && (*a != ' '))
      a++;
    last = a;
    tok[0] = '\0';
    if (first != last) {
      char *pos;

      strncat(tok, first, (int) (last - first));
      /* analyser */
      if ((pos = strstr(months, tok))) {        /* month always in letters */
        result_mm = ((int) (pos - months)) / 4;
      } else {
        int number;

        if (sscanf(tok, "%d", &number) == 1) {  /* number token */
          if (result_dd < 0)    /* day always first number */
            result_dd = number;
          else if (result_n1 < 0)
            result_n1 = number;
          else if (result_n2 < 0)
            result_n2 = number;
          else if (result_n3 < 0)
            result_n3 = number;
          else if (result_n4 < 0)
            result_n4 = number;
        }                       /* sinon, bruit de fond(+1GMT for exampel) */
      }
    }
  }
  if ((result_n1 >= 0) && (result_mm >= 0) && (result_dd >= 0)
      && (result_n2 >= 0) && (result_n3 >= 0) && (result_n4 >= 0)) {
    if (result_n4 >= 1000) {    /* Sun Nov  6 08:49:37 1994 */
      result->tm_year = result_n4 - 1900;
      result->tm_hour = result_n1;
      result->tm_min = result_n2;
      result->tm_sec = max(result_n3, 0);
    } else {                    /* Sun, 06 Nov 1994 08:49:37 GMT or Sunday, 06-Nov-94 08:49:37 GMT */
      result->tm_hour = result_n2;
      result->tm_min = result_n3;
      result->tm_sec = max(result_n4, 0);
      if (result_n1 <= 50)      /* 00 means 2000 */
        result->tm_year = result_n1 + 100;
      else if (result_n1 < 1000)        /* 99 means 1999 */
        result->tm_year = result_n1;
      else                      /* 2000 */
        result->tm_year = result_n1 - 1900;
    }
    result->tm_isdst = 0;       /* assume GMT */
    result->tm_yday = -1;       /* don't know */
    result->tm_wday = -1;       /* don't know */
    result->tm_mon = result_mm;
    result->tm_mday = result_dd;
    return result;
  }
  return NULL;
}
HTS_UNUSED static struct tm PT_GetTime(time_t t) {
  struct tm tmbuf;

#ifdef _WIN32
  struct tm *tm = gmtime(&t);
#else
  struct tm *tm = gmtime_r(&t, &tmbuf);
#endif
  if (tm != NULL)
    return *tm;
  else {
    memset(&tmbuf, 0, sizeof(tmbuf));
    return tmbuf;
  }
}
HTS_UNUSED static int set_filetime(const char *file, struct tm *tm_time) {
  struct utimbuf tim;

#ifndef HTS_DO_NOT_USE_FTIME
  struct timeb B;

  memset(&B, 0, sizeof(B));
  B.timezone = 0;
  ftime(&B);
  tim.actime = tim.modtime = mktime(tm_time) - B.timezone * 60;
#else
  // bogus time (GMT/local)..
  tim.actime = tim.modtime = mktime(tm_time);
#endif
  return utime(file, &tim);
}
HTS_UNUSED static int set_filetime_time_t(const char *file, time_t t) {
  if (t != (time_t) 0 && t != (time_t) - 1) {
    struct tm tm = PT_GetTime(t);

    return set_filetime(file, &tm);
  }
  return -1;
}
HTS_UNUSED static int set_filetime_rfc822(const char *file, const char *date) {
  struct tm buffer;
  struct tm *tm_s = convert_time_rfc822(&buffer, date);

  if (tm_s) {
    return set_filetime(file, tm_s);
  } else
    return -1;
}

#endif
