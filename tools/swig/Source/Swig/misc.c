/* ----------------------------------------------------------------------------- 
 * This file is part of SWIG, which is licensed as a whole under version 3 
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at http://www.swig.org/legal.html.
 *
 * misc.c
 *
 * Miscellaneous functions that don't really fit anywhere else.
 * ----------------------------------------------------------------------------- */

#include "swig.h"
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFDIR) == S_IFDIR)
#endif
#endif

static char *fake_version = 0;

/* -----------------------------------------------------------------------------
 * Swig_copy_string()
 *
 * Duplicate a NULL-terminate string given as a char *.
 * ----------------------------------------------------------------------------- */

char *Swig_copy_string(const char *s) {
  char *c = 0;
  if (s) {
    c = (char *) malloc(strlen(s) + 1);
    strcpy(c, s);
  }
  return c;
}

/* -----------------------------------------------------------------------------
 * Swig_set_fakeversion()
 *
 * Version string override
 * ----------------------------------------------------------------------------- */

void Swig_set_fakeversion(const char *version) {
  fake_version = Swig_copy_string(version);
}

/* -----------------------------------------------------------------------------
 * Swig_package_version()
 *
 * Return the package string containing the version number
 * ----------------------------------------------------------------------------- */

const char *Swig_package_version(void) {
  return fake_version ? fake_version : PACKAGE_VERSION;
}

/* -----------------------------------------------------------------------------
 * Swig_banner()
 *
 * Emits the SWIG identifying banner for the C/C++ wrapper file.
 * ----------------------------------------------------------------------------- */

void Swig_banner(File *f) {
  Printf(f, "/* ----------------------------------------------------------------------------\n\
 * This file was automatically generated by SWIG (http://www.swig.org).\n\
 * Version %s\n\
 * \n\
 * This file is not intended to be easily readable and contains a number of \n\
 * coding conventions designed to improve portability and efficiency. Do not make\n\
 * changes to this file unless you know what you are doing--modify the SWIG \n\
 * interface file instead. \n", Swig_package_version());
  /* String too long for ISO compliance */
  Printf(f, " * ----------------------------------------------------------------------------- */\n");

}

/* -----------------------------------------------------------------------------
 * Swig_banner_target_lang()
 *
 * Emits a SWIG identifying banner in the target language
 * ----------------------------------------------------------------------------- */

void Swig_banner_target_lang(File *f, const_String_or_char_ptr commentchar) {
  Printf(f, "%s This file was automatically generated by SWIG (http://www.swig.org).\n", commentchar);
  Printf(f, "%s Version %s\n", commentchar, Swig_package_version());
  Printf(f, "%s\n", commentchar);
  Printf(f, "%s Do not make changes to this file unless you know what you are doing--modify\n", commentchar);
  Printf(f, "%s the SWIG interface file instead.\n", commentchar);
}

/* -----------------------------------------------------------------------------
 * Swig_strip_c_comments()
 *
 * Return a new string with C comments stripped from the input string. NULL is
 * returned if there aren't any comments.
 * ----------------------------------------------------------------------------- */

String *Swig_strip_c_comments(const String *s) {
  const char *c = Char(s);
  const char *comment_begin = 0;
  const char *comment_end = 0;
  String *stripped = 0;

  while (*c) {
    if (!comment_begin && *c == '/') {
      ++c;
      if (!*c)
        break;
      if (*c == '*')
        comment_begin = c-1;
    } else if (comment_begin && !comment_end && *c == '*') {
      ++c;
      if (*c == '/') {
        comment_end = c;
        break;
      }
    }
    ++c;
  }

  if (comment_begin && comment_end) {
    int size = comment_begin - Char(s);
    String *stripmore = 0;
    stripped = NewStringWithSize(s, size);
    Printv(stripped, comment_end + 1, NIL);
    do {
      stripmore = Swig_strip_c_comments(stripped);
      if (stripmore) {
        Delete(stripped);
        stripped = stripmore;
      }
    } while (stripmore);
  }
  return stripped;
}

/* -----------------------------------------------------------------------------
 * is_directory()
 * ----------------------------------------------------------------------------- */
static int is_directory(String *directory) {
  int last = Len(directory) - 1;
  int statres;
  struct stat st;
  char *dir = Char(directory);
  if (dir[last] == SWIG_FILE_DELIMITER[0]) {
    /* remove trailing slash - can cause S_ISDIR to fail on Windows, at least */
    dir[last] = 0;
    statres = stat(dir, &st);
    dir[last] = SWIG_FILE_DELIMITER[0];
  } else {
    statres = stat(dir, &st);
  }
  return (statres == 0 && S_ISDIR(st.st_mode));
}

/* -----------------------------------------------------------------------------
 * Swig_new_subdirectory()
 *
 * Create the subdirectory only if the basedirectory already exists as a directory.
 * basedirectory can be empty to indicate current directory but not NULL.
 * ----------------------------------------------------------------------------- */

String *Swig_new_subdirectory(String *basedirectory, String *subdirectory) {
  String *error = 0;
  int current_directory = Len(basedirectory) == 0;

  if (current_directory || is_directory(basedirectory)) {
    Iterator it;
    String *dir = NewString(basedirectory);
    List *subdirs = Split(subdirectory, SWIG_FILE_DELIMITER[0], INT_MAX);

    for (it = First(subdirs); it.item; it = Next(it)) {
      int result;
      String *subdirectory = it.item;
      Printf(dir, "%s", subdirectory);
#ifdef _WIN32
      result = _mkdir(Char(dir));
#else
      result = mkdir(Char(dir), 0777);
#endif
      if (result != 0 && errno != EEXIST) {
	error = NewStringf("Cannot create directory %s: %s", dir, strerror(errno));
	break;
      }
      if (!is_directory(dir)) {
	error = NewStringf("Cannot create directory %s: it may already exist but not be a directory", dir);
	break;
      }
      Printf(dir, SWIG_FILE_DELIMITER);
    }
  } else {
    error = NewStringf("Cannot create subdirectory %s under the base directory %s. Either the base does not exist as a directory or it is not readable.", subdirectory, basedirectory);
  }
  return error;
}

/* -----------------------------------------------------------------------------
 * Swig_filename_correct()
 *
 * Corrects filename paths by removing duplicate delimeters and on non-unix
 * systems use the correct delimeter across the whole name.
 * ----------------------------------------------------------------------------- */

void Swig_filename_correct(String *filename) {
  int network_path = 0;
  if (Len(filename) >= 2) {
    const char *fname = Char(filename);
    if (fname[0] == '\\' && fname[1] == '\\')
      network_path = 1;
    if (fname[0] == '/' && fname[1] == '/')
      network_path = 1;
  }
#if defined(_WIN32) || defined(MACSWIG)
  /* accept Unix path separator on non-Unix systems */
  Replaceall(filename, "/", SWIG_FILE_DELIMITER);
#endif
#if defined(__CYGWIN__)
  /* accept Windows path separator in addition to Unix path separator */
  Replaceall(filename, "\\", SWIG_FILE_DELIMITER);
#endif
  /* remove all duplicate file name delimiters */
  while (Replaceall(filename, SWIG_FILE_DELIMITER SWIG_FILE_DELIMITER, SWIG_FILE_DELIMITER)) {
  }
  /* Network paths can start with a double slash on Windows - unremove the duplicate slash we just removed */
  if (network_path)
    Insert(filename, 0, SWIG_FILE_DELIMITER);
}

/* -----------------------------------------------------------------------------
 * Swig_filename_escape()
 *
 * Escapes backslashes in filename - for Windows
 * ----------------------------------------------------------------------------- */

String *Swig_filename_escape(String *filename) {
  String *adjusted_filename = Copy(filename);
  Swig_filename_correct(adjusted_filename);
#if defined(_WIN32)		/* Note not on Cygwin else filename is displayed with double '/' */
  Replaceall(adjusted_filename, "\\", "\\\\");
#endif
  return adjusted_filename;
}

/* -----------------------------------------------------------------------------
 * Swig_filename_unescape()
 *
 * Remove double backslash escaping in filename - for Windows
 * ----------------------------------------------------------------------------- */

void Swig_filename_unescape(String *filename) {
  (void)filename;
#if defined(_WIN32)
  Replaceall(filename, "\\\\", "\\");
#endif
}

/* -----------------------------------------------------------------------------
 * Swig_string_escape()
 *
 * Takes a string object and produces a string with escape codes added to it.
 * ----------------------------------------------------------------------------- */

String *Swig_string_escape(String *s) {
  String *ns;
  int c;
  ns = NewStringEmpty();

  while ((c = Getc(s)) != EOF) {
    if (c == '\n') {
      Printf(ns, "\\n");
    } else if (c == '\r') {
      Printf(ns, "\\r");
    } else if (c == '\t') {
      Printf(ns, "\\t");
    } else if (c == '\\') {
      Printf(ns, "\\\\");
    } else if (c == '\'') {
      Printf(ns, "\\'");
    } else if (c == '\"') {
      Printf(ns, "\\\"");
    } else if (c == ' ') {
      Putc(c, ns);
    } else if (!isgraph(c)) {
      if (c < 0)
	c += UCHAR_MAX + 1;
      Printf(ns, "\\%o", c);
    } else {
      Putc(c, ns);
    }
  }
  return ns;
}


/* -----------------------------------------------------------------------------
 * Swig_string_upper()
 *
 * Takes a string object and returns a copy that is uppercase
 * ----------------------------------------------------------------------------- */

String *Swig_string_upper(String *s) {
  String *ns;
  int c;
  ns = NewStringEmpty();

  Seek(s, 0, SEEK_SET);
  while ((c = Getc(s)) != EOF) {
    Putc(toupper(c), ns);
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_lower()
 *
 * Takes a string object and returns a copy that is lowercase
 * ----------------------------------------------------------------------------- */

String *Swig_string_lower(String *s) {
  String *ns;
  int c;
  ns = NewStringEmpty();

  Seek(s, 0, SEEK_SET);
  while ((c = Getc(s)) != EOF) {
    Putc(tolower(c), ns);
  }
  return ns;
}


/* -----------------------------------------------------------------------------
 * Swig_string_title()
 *
 * Takes a string object and returns a copy that is lowercase with first letter
 * capitalized
 * ----------------------------------------------------------------------------- */

String *Swig_string_title(String *s) {
  String *ns;
  int first = 1;
  int c;
  ns = NewStringEmpty();

  Seek(s, 0, SEEK_SET);
  while ((c = Getc(s)) != EOF) {
    Putc(first ? toupper(c) : tolower(c), ns);
    first = 0;
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_ccase()
 *
 * Takes a string object and returns a copy that is lowercase with the first
 * letter capitalized and the one following '_', which are removed.
 *
 *      camel_case -> CamelCase
 *      camelCase  -> CamelCase
 * ----------------------------------------------------------------------------- */

String *Swig_string_ccase(String *s) {
  String *ns;
  int first = 1;
  int c;
  ns = NewStringEmpty();

  Seek(s, 0, SEEK_SET);
  while ((c = Getc(s)) != EOF) {
    if (c == '_') {
      first = 1;
      continue;
    }
    Putc(first ? toupper(c) : c, ns);
    first = 0;
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_lccase()
 *
 * Takes a string object and returns a copy with the character after
 * each '_' capitalised, and the '_' removed.  The first character is
 * also forced to lowercase.
 *
 *      camel_case -> camelCase
 *      CamelCase  -> camelCase
 * ----------------------------------------------------------------------------- */

String *Swig_string_lccase(String *s) {
  String *ns;
  int first = 1;
  int after_underscore = 0;
  int c;
  ns = NewStringEmpty();

  Seek(s, 0, SEEK_SET);
  while ((c = Getc(s)) != EOF) {
    if (c == '_') {
      after_underscore = 1;
      continue;
    }
    if (first) {
      Putc(tolower(c), ns);
      first = 0;
    } else {
      Putc(after_underscore ? toupper(c) : c, ns);
    }
    after_underscore = 0;
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_ucase()
 *
 * This is the reverse case of ccase, ie
 *
 *      CamelCase -> camel_case
 *      get2D     -> get_2d
 *      asFloat2  -> as_float2
 * ----------------------------------------------------------------------------- */

String *Swig_string_ucase(String *s) {
  String *ns;
  int c;
  int lastC = 0;
  int nextC = 0;
  int underscore = 0;
  ns = NewStringEmpty();

  /* We insert a underscore when:
     1. Lower case char followed by upper case char
     getFoo > get_foo; getFOo > get_foo; GETFOO > getfoo
     2. Number proceded by char and not end of string
     get2D > get_2d; get22D > get_22d; GET2D > get_2d
     but:
     asFloat2 > as_float2
  */

  Seek(s, 0, SEEK_SET);

  while ((c = Getc(s)) != EOF) {
    nextC = Getc(s); Ungetc(nextC, s);
    if (isdigit(c) && isalpha(lastC) && nextC != EOF)
      underscore = 1;
    else if (isupper(c) && isalpha(lastC) && !isupper(lastC))
      underscore = 1;

    lastC = c;

    if (underscore) {
      Putc('_', ns);
      underscore = 0;
    }

    Putc(tolower(c), ns);
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_first_upper()
 *
 * Make the first character in the string uppercase, leave all the 
 * rest the same.  This is used by the Ruby module to provide backwards
 * compatibility with the old way of naming classes and constants.  For
 * more info see the Ruby documentation.
 *
 *      firstUpper -> FirstUpper 
 * ----------------------------------------------------------------------------- */

String *Swig_string_first_upper(String *s) {
  String *ns = NewStringEmpty();
  char *cs = Char(s);
  if (cs && cs[0] != 0) {
    Putc(toupper((int)cs[0]), ns);
    Append(ns, cs + 1);
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_first_lower()
 *
 * Make the first character in the string lowercase, leave all the 
 * rest the same.  This is used by the Ruby module to provide backwards
 * compatibility with the old way of naming classes and constants.  For
 * more info see the Ruby documentation.
 *
 *      firstLower -> FirstLower 
 * ----------------------------------------------------------------------------- */

String *Swig_string_first_lower(String *s) {
  String *ns = NewStringEmpty();
  char *cs = Char(s);
  if (cs && cs[0] != 0) {
    Putc(tolower((int)cs[0]), ns);
    Append(ns, cs + 1);
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_schemify()
 *
 * Replace underscores with dashes, to make identifiers look nice to Schemers.
 *
 *      under_scores -> under-scores
 * ----------------------------------------------------------------------------- */

String *Swig_string_schemify(String *s) {
  String *ns = NewString(s);
  Replaceall(ns, "_", "-");
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_typecode()
 *
 * Takes a string with possible type-escapes in it and replaces them with
 * real C datatypes.
 * ----------------------------------------------------------------------------- */

String *Swig_string_typecode(String *s) {
  String *ns;
  int c;
  String *tc;
  ns = NewStringEmpty();
  while ((c = Getc(s)) != EOF) {
    if (c == '`') {
      String *str = 0;
      tc = NewStringEmpty();
      while ((c = Getc(s)) != EOF) {
	if (c == '`')
	  break;
	Putc(c, tc);
      }
      str = SwigType_str(tc, 0);
      Append(ns, str);
      Delete(str);
    } else {
      Putc(c, ns);
      if (c == '\'') {
	while ((c = Getc(s)) != EOF) {
	  Putc(c, ns);
	  if (c == '\'')
	    break;
	  if (c == '\\') {
	    c = Getc(s);
	    Putc(c, ns);
	  }
	}
      } else if (c == '\"') {
	while ((c = Getc(s)) != EOF) {
	  Putc(c, ns);
	  if (c == '\"')
	    break;
	  if (c == '\\') {
	    c = Getc(s);
	    Putc(c, ns);
	  }
	}
      }
    }
  }
  return ns;
}

/* -----------------------------------------------------------------------------
 * Swig_string_mangle()
 * 
 * Take a string and mangle it by stripping all non-valid C identifier
 * characters.
 *
 * This routine skips unnecessary blank spaces, therefore mangling
 * 'char *' and 'char*', 'std::pair<int, int >' and
 * 'std::pair<int,int>', produce the same result.
 *
 * However, note that 'long long' and 'long_long' produce different
 * mangled strings.
 *
 * The mangling method still is not 'perfect', for example std::pair and
 * std_pair return the same mangling. This is just a little better
 * than before, but it seems to be enough for most of the purposes.
 *
 * Having a perfect mangling will break some examples and code which
 * assume, for example, that A::get_value will be mangled as
 * A_get_value. 
 * ----------------------------------------------------------------------------- */

String *Swig_string_mangle(const String *s) {
#if 0
  /* old mangling, not suitable for using in macros */
  String *t = Copy(s);
  char *c = Char(t);
  while (*c) {
    if (!isalnum(*c))
      *c = '_';
    c++;
  }
  return t;
#else
  String *result = NewStringEmpty();
  int space = 0;
  int state = 0;
  char *pc, *cb;
  String *b = Copy(s);
  if (SwigType_istemplate(b)) {
    String *st = Swig_symbol_template_deftype(b, 0);
    String *sq = Swig_symbol_type_qualify(st, 0);
    String *t = SwigType_namestr(sq);
    Delete(st);
    Delete(sq);
    Delete(b);
    b = t;
  }
  pc = cb = Char(b);
  while (*pc) {
    char c = *pc;
    if (isalnum((int) c) || (c == '_')) {
      state = 1;
      if (space && (space == state)) {
	Append(result, "_SS_");
      }
      space = 0;
      Printf(result, "%c", (int) c);

    } else {
      if (isspace((int) c)) {
	space = state;
	++pc;
	continue;
      } else {
	state = 3;
	space = 0;
      }
      switch (c) {
      case '.':
	if ((cb != pc) && (*(pc - 1) == 'p')) {
	  Append(result, "_");
	  ++pc;
	  continue;
	} else {
	  c = 'f';
	}
	break;
      case ':':
	if (*(pc + 1) == ':') {
	  Append(result, "_");
	  ++pc;
	  ++pc;
	  continue;
	}
	break;
      case '*':
	c = 'm';
	break;
      case '&':
	c = 'A';
	break;
      case '<':
	c = 'l';
	break;
      case '>':
	c = 'g';
	break;
      case '=':
	c = 'e';
	break;
      case ',':
	c = 'c';
	break;
      case '(':
	c = 'p';
	break;
      case ')':
	c = 'P';
	break;
      case '[':
	c = 'b';
	break;
      case ']':
	c = 'B';
	break;
      case '^':
	c = 'x';
	break;
      case '|':
	c = 'o';
	break;
      case '~':
	c = 'n';
	break;
      case '!':
	c = 'N';
	break;
      case '%':
	c = 'M';
	break;
      case '?':
	c = 'q';
	break;
      case '+':
	c = 'a';
	break;
      case '-':
	c = 's';
	break;
      case '/':
	c = 'd';
	break;
      default:
	break;
      }
      if (isalpha((int) c)) {
	Printf(result, "_S%c_", (int) c);
      } else {
	Printf(result, "_S%02X_", (int) c);
      }
    }
    ++pc;
  }
  Delete(b);
  return result;
#endif
}

String *Swig_string_emangle(String *s) {
  return Swig_string_mangle(s);
}


/* -----------------------------------------------------------------------------
 * Swig_scopename_prefix()
 *
 * Take a qualified name like "A::B::C" and return the scope name.
 * In this case, "A::B".   Returns NULL if there is no base.
 * ----------------------------------------------------------------------------- */

void Swig_scopename_split(const String *s, String **rprefix, String **rlast) {
  char *tmp = Char(s);
  char *c = tmp;
  char *cc = c;
  char *co = 0;
  if (!strstr(c, "::")) {
    *rprefix = 0;
    *rlast = Copy(s);
  }

  co = strstr(cc, "operator ");
  if (co) {
    if (co == cc) {
      *rprefix = 0;
      *rlast = Copy(s);
      return;
    } else {
      *rprefix = NewStringWithSize(cc, co - cc - 2);
      *rlast = NewString(co);
      return;
    }
  }
  while (*c) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      cc = c;
      c += 2;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }

  if (cc != tmp) {
    *rprefix = NewStringWithSize(tmp, cc - tmp);
    *rlast = NewString(cc + 2);
    return;
  } else {
    *rprefix = 0;
    *rlast = Copy(s);
  }
}


String *Swig_scopename_prefix(const String *s) {
  char *tmp = Char(s);
  char *c = tmp;
  char *cc = c;
  char *co = 0;
  if (!strstr(c, "::"))
    return 0;
  co = strstr(cc, "operator ");

  if (co) {
    if (co == cc) {
      return 0;
    } else {
      String *prefix = NewStringWithSize(cc, co - cc - 2);
      return prefix;
    }
  }
  while (*c) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      cc = c;
      c += 2;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }

  if (cc != tmp) {
    return NewStringWithSize(tmp, cc - tmp);
  } else {
    return 0;
  }
}

/* -----------------------------------------------------------------------------
 * Swig_scopename_last()
 *
 * Take a qualified name like "A::B::C" and returns the last.  In this
 * case, "C". 
 * ----------------------------------------------------------------------------- */

String *Swig_scopename_last(const String *s) {
  char *tmp = Char(s);
  char *c = tmp;
  char *cc = c;
  char *co = 0;
  if (!strstr(c, "::"))
    return NewString(s);

  co = strstr(cc, "operator ");
  if (co) {
    return NewString(co);
  }


  while (*c) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      c += 2;
      cc = c;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }
  return NewString(cc);
}

/* -----------------------------------------------------------------------------
 * Swig_scopename_first()
 *
 * Take a qualified name like "A::B::C" and returns the first scope name.
 * In this case, "A".   Returns NULL if there is no base.
 * ----------------------------------------------------------------------------- */

String *Swig_scopename_first(const String *s) {
  char *tmp = Char(s);
  char *c = tmp;
  char *co = 0;
  if (!strstr(c, "::"))
    return 0;

  co = strstr(c, "operator ");
  if (co) {
    if (co == c) {
      return 0;
    }
  } else {
    co = c + Len(s);
  }

  while (*c && (c != co)) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      break;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }
  if (*c && (c != tmp)) {
    return NewStringWithSize(tmp, c - tmp);
  } else {
    return 0;
  }
}


/* -----------------------------------------------------------------------------
 * Swig_scopename_suffix()
 *
 * Take a qualified name like "A::B::C" and returns the suffix.
 * In this case, "B::C".   Returns NULL if there is no suffix.
 * ----------------------------------------------------------------------------- */

String *Swig_scopename_suffix(const String *s) {
  char *tmp = Char(s);
  char *c = tmp;
  char *co = 0;
  if (!strstr(c, "::"))
    return 0;

  co = strstr(c, "operator ");
  if (co) {
    if (co == c)
      return 0;
  }
  while (*c) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      break;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }
  if (*c && (c != tmp)) {
    return NewString(c + 2);
  } else {
    return 0;
  }
}

/* -----------------------------------------------------------------------------
 * Swig_scopename_check()
 *
 * Checks to see if a name is qualified with a scope name, examples:
 *   foo            -> 0
 *   ::foo          -> 1
 *   foo::bar       -> 1
 *   foo< ::bar >   -> 0
 * ----------------------------------------------------------------------------- */

int Swig_scopename_check(const String *s) {
  char *c = Char(s);
  char *co = strstr(c, "operator ");

  if (co) {
    if (co == c)
      return 0;
  }
  if (!strstr(c, "::"))
    return 0;
  while (*c) {
    if ((*c == ':') && (*(c + 1) == ':')) {
      return 1;
    } else {
      if (*c == '<') {
	int level = 1;
	c++;
	while (*c && level) {
	  if (*c == '<')
	    level++;
	  if (*c == '>')
	    level--;
	  c++;
	}
      } else {
	c++;
      }
    }
  }
  return 0;
}

/* -----------------------------------------------------------------------------
 * Swig_string_command()
 *
 * Executes a external command via popen with the string as a command
 * line parameter. For example:
 *
 *  Printf(stderr,"%(command:sed 's/[a-z]/\U\\1/' <<<)s","hello") -> Hello
 * ----------------------------------------------------------------------------- */
#if defined(HAVE_POPEN)
#  if defined(_MSC_VER)
#    define popen _popen
#    define pclose _pclose
#  else
extern FILE *popen(const char *command, const char *type);
extern int pclose(FILE *stream);
#  endif
#else
#  if defined(_MSC_VER)
#    define HAVE_POPEN 1
#    define popen _popen
#    define pclose _pclose
#  endif
#endif

String *Swig_string_command(String *s) {
  String *res = NewStringEmpty();
#if defined(HAVE_POPEN)
  if (Len(s)) {
    char *command = Char(s);
    FILE *fp = popen(command, "r");
    if (fp) {
      char buffer[1025];
      while (fscanf(fp, "%1024s", buffer) != EOF) {
	Append(res, buffer);
      }
      pclose(fp);
    } else {
      Swig_error("SWIG", Getline(s), "Command encoder fails attempting '%s'.\n", s);
      exit(1);
    }
  }
#endif
  return res;
}


/* -----------------------------------------------------------------------------
 * Swig_string_strip()
 *
 * Strip given prefix from identifiers 
 *
 *  Printf(stderr,"%(strip:[wx])s","wxHello") -> Hello
 * ----------------------------------------------------------------------------- */

String *Swig_string_strip(String *s) {
  String *ns;
  if (!Len(s)) {
    ns = NewString(s);
  } else {
    const char *cs = Char(s);
    const char *ce = Strchr(cs, ']');
    if (*cs != '[' || !ce) {
      ns = NewString(s);
    } else {
      String *fmt = NewStringf("%%.%ds", ce-cs-1);
      String *prefix = NewStringf(fmt, cs+1);
      if (0 == Strncmp(ce+1, prefix, Len(prefix))) {
        ns = NewString(ce+1+Len(prefix));
      } else {
        ns = NewString(ce+1);
      }
    }
  }
  return ns;
}


#ifdef HAVE_PCRE
#include <pcre.h>

static int split_regex_pattern_subst(String *s, String **pattern, String **subst, const char **input)
{
  const char *pats, *pate;
  const char *subs, *sube;

  /* Locate the search pattern */
  const char *p = Char(s);
  if (*p++ != '/') goto err_out;
  pats = p;
  p = strchr(p, '/');
  if (!p) goto err_out;
  pate = p;

  /* Locate the substitution string */
  subs = ++p;
  p = strchr(p, '/');
  if (!p) goto err_out;
  sube = p;

  *pattern = NewStringWithSize(pats, pate - pats);
  *subst   = NewStringWithSize(subs, sube - subs);
  *input   = p + 1;
  return 1;

err_out:
  Swig_error("SWIG", Getline(s), "Invalid regex substitution: '%s'.\n", s);
  exit(1);
}

String *replace_captures(int num_captures, const char *input, String *subst, int captures[], String *pattern, String *s)
{
  String *result = NewStringEmpty();
  const char *p = Char(subst);

  while (*p) {
    /* Copy part without substitutions */
    const char *q = strchr(p, '\\');
    if (!q) {
      Write(result, p, strlen(p));
      break;
    }
    Write(result, p, q - p);
    p = q + 1;

    /* Handle substitution */
    if (*p == '\0') {
      Putc('\\', result);
    } else if (isdigit((unsigned char)*p)) {
      int group = *p++ - '0';
      if (group < num_captures) {
	int l = captures[group*2], r = captures[group*2 + 1];
	if (l != -1) {
	  Write(result, input + l, r - l);
	}
      } else {
	Swig_error("SWIG", Getline(s), "PCRE capture replacement failed while matching \"%s\" using \"%s\" - request for group %d is greater than the number of captures %d.\n",
	    Char(pattern), input, group, num_captures-1);
      }
    }
  }

  return result;
}

/* -----------------------------------------------------------------------------
 * Swig_string_regex()
 *
 * Executes a regular expression substitution. For example:
 *
 *   Printf(stderr,"gsl%(regex:/GSL_.*_/\\1/)s","GSL_Hello_") -> gslHello
 * ----------------------------------------------------------------------------- */
String *Swig_string_regex(String *s) {
  const int pcre_options = 0;

  String *res = 0;
  pcre *compiled_pat = 0;
  const char *pcre_error, *input;
  int pcre_errorpos;
  String *pattern = 0, *subst = 0;
  int captures[30];

  if (split_regex_pattern_subst(s, &pattern, &subst, &input)) {
    int rc;

    compiled_pat = pcre_compile(
          Char(pattern), pcre_options, &pcre_error, &pcre_errorpos, NULL);
    if (!compiled_pat) {
      Swig_error("SWIG", Getline(s), "PCRE compilation failed: '%s' in '%s':%i.\n",
          pcre_error, Char(pattern), pcre_errorpos);
      exit(1);
    }
    rc = pcre_exec(compiled_pat, NULL, input, strlen(input), 0, 0, captures, 30);
    if (rc >= 0) {
      res = replace_captures(rc, input, subst, captures, pattern, s);
    } else if (rc != PCRE_ERROR_NOMATCH) {
      Swig_error("SWIG", Getline(s), "PCRE execution failed: error %d while matching \"%s\" using \"%s\".\n",
	rc, Char(pattern), input);
      exit(1);
    }
  }

  DohDelete(pattern);
  DohDelete(subst);
  pcre_free(compiled_pat);
  return res ? res : NewStringEmpty();
}

String *Swig_pcre_version(void) {
  return NewStringf("PCRE Version: %s", pcre_version());
}

#else

String *Swig_string_regex(String *s) {
  Swig_error("SWIG", Getline(s), "PCRE regex support not enabled in this SWIG build.\n");
  exit(1);
}

String *Swig_pcre_version(void) {
  return NewStringf("PCRE not used");
}

#endif

/* -----------------------------------------------------------------------------
 * Swig_init()
 *
 * Initialize the SWIG core
 * ----------------------------------------------------------------------------- */

void Swig_init() {
  /* Set some useful string encoding methods */
  DohEncoding("escape", Swig_string_escape);
  DohEncoding("upper", Swig_string_upper);
  DohEncoding("lower", Swig_string_lower);
  DohEncoding("title", Swig_string_title);
  DohEncoding("ctitle", Swig_string_ccase);
  DohEncoding("lctitle", Swig_string_lccase);
  DohEncoding("utitle", Swig_string_ucase);
  DohEncoding("typecode", Swig_string_typecode);
  DohEncoding("mangle", Swig_string_emangle);
  DohEncoding("command", Swig_string_command);
  DohEncoding("schemify", Swig_string_schemify);
  DohEncoding("strip", Swig_string_strip);
  DohEncoding("regex", Swig_string_regex);

  /* aliases for the case encoders */
  DohEncoding("uppercase", Swig_string_upper);
  DohEncoding("lowercase", Swig_string_lower);
  DohEncoding("camelcase", Swig_string_ccase);
  DohEncoding("lowercamelcase", Swig_string_lccase);
  DohEncoding("undercase", Swig_string_ucase);
  DohEncoding("firstuppercase", Swig_string_first_upper);
  DohEncoding("firstlowercase", Swig_string_first_lower);

  /* Initialize typemaps */
  Swig_typemap_init();

  /* Initialize symbol table */
  Swig_symbol_init();

  /* Initialize type system */
  SwigType_typesystem_init();

  /* Initialize template system */
  SwigType_template_init();
}
