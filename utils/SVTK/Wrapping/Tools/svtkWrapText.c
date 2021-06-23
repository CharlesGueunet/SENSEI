/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkWrapText.c

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkWrapText.h"
#include "svtkWrap.h"

#include "svtkParseExtras.h"
#include "svtkParseMangle.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------- */
/* Convert special characters in a string into their escape codes
 * so that the string can be quoted in a source file.  The specified
 * maxlen must be at least 32 chars, and should not be over 2047 since
 * that is the maximum length of a string literal on some systems */

const char* svtkWrapText_QuoteString(const char* comment, size_t maxlen)
{
  static char* result = 0;
  static size_t oldmaxlen = 0;
  size_t i = 0;
  size_t j = 0;
  size_t k, n, m;
  unsigned short x;

  if (maxlen > oldmaxlen)
  {
    if (result)
    {
      free(result);
    }
    result = (char*)malloc((size_t)(maxlen + 1));
    oldmaxlen = maxlen;
  }

  if (comment == NULL)
  {
    return "";
  }

  while (comment[i] != '\0')
  {
    n = 1; /* "n" stores number of input bytes consumed */
    m = 1; /* "m" stores number of output bytes written */

    if ((comment[i] & 0x80) != 0)
    {
      /* check for trailing bytes in utf-8 sequence */
      while ((comment[i + n] & 0xC0) == 0x80)
      {
        n++;
      }

      /* the first two bytes will be used to check for validity */
      x = (((unsigned char)(comment[i]) << 8) | (unsigned char)(comment[i + 1]));

      /* check for valid 2, 3, or 4 byte utf-8 sequences */
      if ((n == 2 && x >= 0xC280 && x < 0xE000) ||
        (n == 3 && x >= 0xE0A0 && x < 0xF000 && (x >= 0xEE80 || x < 0xEDA0)) ||
        (n == 4 && x >= 0xF090 && x < 0xF490))
      {
        /* write the valid utf-8 sequence */
        for (k = 0; k < n; k++)
        {
          sprintf(&result[j + 4 * k], "\\%3.3o", (unsigned char)(comment[i + k]));
        }
        m = 4 * n;
      }
      else
      {
        /* bad sequence, write the replacement character code U+FFFD */
        sprintf(&result[j], "%s", "\\357\\277\\275");
        m = 12;
      }
    }
    else if (comment[i] == '\"' || comment[i] == '\\')
    {
      result[j] = '\\';
      result[j + 1] = comment[i];
      m = 2;
    }
    else if (isprint(comment[i]))
    {
      result[j] = comment[i];
    }
    else if (comment[i] == '\n')
    {
      result[j] = '\\';
      result[j + 1] = 'n';
      m = 2;
    }
    else
    {
      /* use octal escape sequences for other control codes */
      sprintf(&result[j], "\\%3.3o", comment[i]);
      m = 4;
    }

    /* check if output limit is reached */
    if (j + m >= maxlen - 20)
    {
      sprintf(&result[j], " ...\\n [Truncated]\\n");
      j += strlen(" ...\\n [Truncated]\\n");
      break;
    }

    i += n;
    j += m;
  }

  result[j] = '\0';

  return result;
}

/* -------------------------------------------------------------------- */
/* A simple string that grows as necessary. */

struct svtkWPString
{
  char* str;
  size_t len;
  size_t maxlen;
};

/* -- append ---------- */
static void svtkWPString_Append(struct svtkWPString* str, const char* text)
{
  size_t n = strlen(text);

  if (str->len + n + 1 > str->maxlen)
  {
    str->maxlen = (str->len + n + 1 + 2 * str->maxlen);
    str->str = (char*)realloc(str->str, str->maxlen);
  }

  strncpy(&str->str[str->len], text, n + 1);
  str->len += n;
}

/* -- add a char ---------- */
static void svtkWPString_PushChar(struct svtkWPString* str, char c)
{
  if (str->len + 2 > str->maxlen)
  {
    str->maxlen = (str->len + 2 + 2 * str->maxlen);
    str->str = (char*)realloc(str->str, str->maxlen);
  }

  str->str[str->len++] = c;
  str->str[str->len] = '\0';
}

/* -- strip any of the given chars from the end ---------- */
static void svtkWPString_Strip(struct svtkWPString* str, const char* trailers)
{
  size_t k = str->len;
  char* cp = str->str;
  size_t j = 0;
  size_t n;

  if (cp)
  {
    n = strlen(trailers);

    while (k > 0 && j < n)
    {
      for (j = 0; j < n; j++)
      {
        if (cp[k - 1] == trailers[j])
        {
          k--;
          break;
        }
      }
    }

    str->len = k;
    str->str[k] = '\0';
  }
}

/* -- Return the last char ---------- */
static char svtkWPString_LastChar(struct svtkWPString* str)
{
  if (str->str && str->len > 0)
  {
    return str->str[str->len - 1];
  }
  return '\0';
}

/* -- do a linebreak on a method declaration ---------- */
static void svtkWPString_BreakSignatureLine(
  struct svtkWPString* str, size_t* linestart, size_t indentation)
{
  size_t i = 0;
  size_t m = 0;
  size_t j = *linestart;
  size_t l = str->len;
  size_t k = str->len;
  char* text = str->str;
  char delim;

  if (!text)
  {
    return;
  }

  while (
    l > j && text[l - 1] != '\n' && text[l - 1] != ',' && text[l - 1] != '(' && text[l - 1] != ')')
  {
    /* treat each string as a unit */
    if (l > 4 && (text[l - 1] == '\'' || text[l - 1] == '\"'))
    {
      delim = text[l - 1];
      l -= 2;
      while (l > 3 && (text[l - 1] != delim || text[l - 3] == '\\'))
      {
        l--;
        if (text[l - 1] == '\\')
        {
          l--;
        }
      }
      l -= 2;
    }
    else
    {
      l--;
    }
  }

  /* if none of these chars was found, split is impossible */
  if (text[l - 1] != ',' && text[l - 1] != '(' && text[l - 1] != ')' && text[l - 1] != '\n')
  {
    j++;
  }

  else
  {
    /* Append some chars to guarantee size */
    svtkWPString_PushChar(str, '\n');
    svtkWPString_PushChar(str, '\n');
    for (i = 0; i < indentation; i++)
    {
      svtkWPString_PushChar(str, ' ');
    }
    /* re-get the char pointer, it may have been reallocated */
    text = str->str;

    if (k > l)
    {
      m = 0;
      while (m < indentation + 2 && text[l + m] == ' ')
      {
        m++;
      }
      memmove(&text[l + indentation + 2 - m], &text[l], k - l);
      k += indentation + 2 - m;
    }
    else
    {
      k += indentation + 2;
    }
    text[l++] = '\\';
    text[l++] = 'n';
    j = l;
    for (i = 0; i < indentation; i++)
    {
      text[l++] = ' ';
    }
  }

  str->len = k;

  /* return the new line start position */
  *linestart = j;
}

/* -- do a linebreak on regular text ---------- */
static void svtkWPString_BreakCommentLine(struct svtkWPString* str, size_t* linestart, size_t indent)
{
  size_t i = 0;
  size_t j = *linestart;
  size_t l = str->len;
  char* text = str->str;

  if (!text)
  {
    return;
  }

  /* try to break the line at a word */
  while (l > 0 && text[l - 1] != ' ' && text[l - 1] != '\n')
  {
    l--;
  }
  if (l > 0 && text[l - 1] != '\n' && l - j > indent)
  {
    /* replace space with newline */
    text[l - 1] = '\n';
    j = l;

    /* Append some chars to guarantee size */
    svtkWPString_PushChar(str, '\n');
    svtkWPString_PushChar(str, '\n');
    for (i = 0; i < indent; i++)
    {
      svtkWPString_PushChar(str, ' ');
    }
    /* re-get the char pointer, it may have been reallocated */
    text = str->str;
    str->len -= indent + 2;

    if (str->len > l && indent > 0)
    {
      memmove(&text[l + indent], &text[l], str->len - l);
      memset(&text[l], ' ', indent);
      str->len += indent;
    }
  }
  else
  {
    /* long word, just split the word */
    svtkWPString_PushChar(str, '\n');
    j = str->len;
    for (i = 0; i < indent; i++)
    {
      svtkWPString_PushChar(str, ' ');
    }
  }

  /* return the new line start position */
  *linestart = j;
}

/* -------------------------------------------------------------------- */
/* Format a signature to a 70 char linewidth and char limit */
const char* svtkWrapText_FormatSignature(const char* signature, size_t width, size_t maxlen)
{
  static struct svtkWPString staticString = { NULL, 0, 0 };
  struct svtkWPString* text;
  size_t i, j, n;
  const char* cp = signature;
  char delim;
  size_t lastSigStart = 0;
  size_t sigCount = 0;

  text = &staticString;
  text->len = 0;

  if (signature == 0)
  {
    return "";
  }

  i = 0;
  j = 0;

  while (cp[i] != '\0')
  {
    while (text->len - j < width && cp[i] != '\n' && cp[i] != '\0')
    {
      /* escape quotes */
      if (cp[i] == '\"' || cp[i] == '\'')
      {
        delim = cp[i];
        svtkWPString_PushChar(text, '\\');
        svtkWPString_PushChar(text, cp[i++]);
        while (cp[i] != delim && cp[i] != '\0')
        {
          if (cp[i] == '\\')
          {
            svtkWPString_PushChar(text, '\\');
          }
          svtkWPString_PushChar(text, cp[i++]);
        }
        if (cp[i] == delim)
        {
          svtkWPString_PushChar(text, '\\');
          svtkWPString_PushChar(text, cp[i++]);
        }
      }
      /* remove items that trail the closing parenthesis */
      else if (cp[i] == ')')
      {
        svtkWPString_PushChar(text, cp[i++]);
        if (strncmp(&cp[i], " const", 6) == 0)
        {
          i += 6;
        }
        if (strncmp(&cp[i], " = 0", 4) == 0)
        {
          i += 4;
        }
        if (cp[i] == ';')
        {
          i++;
        }
      }
      /* anything else */
      else
      {
        svtkWPString_PushChar(text, cp[i++]);
      }
    }

    /* break the line (try to break after a comma) */
    if (cp[i] != '\n' && cp[i] != '\0')
    {
      svtkWPString_BreakSignatureLine(text, &j, 4);
    }
    /* reached end of line: do next signature */
    else
    {
      svtkWPString_Strip(text, " \r\t");
      if (cp[i] != '\0')
      {
        sigCount++;
        /* if sig count is even, check length against maxlen */
        if ((sigCount & 1) == 0)
        {
          n = strlen(text->str);
          if (n >= maxlen)
          {
            break;
          }
          lastSigStart = n;
        }

        i++;
        svtkWPString_PushChar(text, '\\');
        svtkWPString_PushChar(text, 'n');
      }
      /* mark the position of the start of the line */
      j = text->len;
    }
  }

  svtkWPString_Strip(text, " \r\t");

  if (strlen(text->str) >= maxlen)
  {
    /* terminate before the current signature */
    text->str[lastSigStart] = '\0';
  }

  return text->str;
}

/* -------------------------------------------------------------------- */
/* Format a comment to a 70 char linewidth, in several steps:
 * 1) remove html tags, convert <p> and <br> into breaks
 * 2) remove doxygen tags like \em
 * 3) remove extra whitespace (except paragraph breaks)
 * 4) re-break the lines
 */

const char* svtkWrapText_FormatComment(const char* comment, size_t width)
{
  static struct svtkWPString staticString = { NULL, 0, 0 };
  struct svtkWPString* text;
  const char* cp;
  size_t i, j, l;
  size_t indent = 0;
  int nojoin = 0;
  int start;

  text = &staticString;
  text->len = 0;

  if (comment == 0)
  {
    return "";
  }

  i = 0;
  j = 0;
  l = 0;
  start = 1;
  cp = comment;

  /* skip any leading whitespace */
  while (cp[i] == '\n' || cp[i] == '\r' || cp[i] == '\t' || cp[i] == ' ')
  {
    i++;
  }

  while (cp[i] != '\0')
  {
    /* Add characters until the output line is complete */
    while (cp[i] != '\0' && text->len - j < width)
    {
      /* if the end of the line was found, see how next line begins */
      if (start)
      {
        /* eat the leading space */
        if (cp[i] == ' ')
        {
          i++;
        }

        /* skip ahead to find any interesting first characters */
        l = i;
        while (cp[l] == ' ' || cp[l] == '\t' || cp[l] == '\r')
        {
          l++;
        }

        /* check for new section */
        if (cp[l] == '.' && strncmp(&cp[l], ".SECTION", 8) == 0)
        {
          svtkWPString_Strip(text, "\n");
          if (text->len > 0)
          {
            svtkWPString_PushChar(text, '\n');
            svtkWPString_PushChar(text, '\n');
          }
          i = l + 8;
          while (cp[i] == '\r' || cp[i] == '\t' || cp[i] == ' ')
          {
            i++;
          }
          while (cp[i] != '\n' && cp[i] != '\0')
          {
            svtkWPString_PushChar(text, cp[i++]);
          }
          svtkWPString_Strip(text, " \t\r");

          if (svtkWPString_LastChar(text) != ':')
          {
            svtkWPString_PushChar(text, ':');
          }
          svtkWPString_PushChar(text, '\n');
          svtkWPString_PushChar(text, '\n');
          j = text->len;
          indent = 0;
          if (cp[i] == '\n')
          {
            i++;
          }
          start = 1;
          continue;
        }

        /* handle doxygen tags that appear at start of line */
        if (cp[l] == '\\' || cp[l] == '@')
        {
          if (strncmp(&cp[l + 1], "brief", 5) == 0 || strncmp(&cp[l + 1], "short", 5) == 0 ||
            strncmp(&cp[l + 1], "pre", 3) == 0 || strncmp(&cp[l + 1], "post", 4) == 0 ||
            strncmp(&cp[l + 1], "param", 5) == 0 || strncmp(&cp[l + 1], "tparam", 6) == 0 ||
            strncmp(&cp[l + 1], "cmdparam", 8) == 0 || strncmp(&cp[l + 1], "exception", 9) == 0 ||
            strncmp(&cp[l + 1], "return", 6) == 0 || strncmp(&cp[l + 1], "warning", 7) == 0 ||
            strncmp(&cp[l + 1], "sa", 2) == 0 || strncmp(&cp[l + 1], "li", 2) == 0)
          {
            nojoin = 2;
            indent = 4;
            if (text->len > 0 && svtkWPString_LastChar(text) != '\n')
            {
              svtkWPString_PushChar(text, '\n');
            }
            j = text->len;
            i = l;

            /* remove these two tags from the output text */
            if (strncmp(&cp[l + 1], "brief", 5) == 0 || strncmp(&cp[l + 1], "short", 5) == 0)
            {
              i = l + 6;
              while (cp[i] == ' ')
              {
                i++;
              }
            }
          }
        }

        /* handle bullets and numbering */
        else if (cp[l] == '-' || cp[l] == '*' || cp[l] == '#' ||
          (cp[l] >= '0' && cp[l] <= '9' && (cp[l + 1] == ')' || cp[l + 1] == '.') &&
            cp[l + 2] == ' '))
        {
          indent = 0;
          while (indent < 3 && cp[l + indent] != ' ')
          {
            indent++;
          }
          indent++;
          if (text->len > 0 && svtkWPString_LastChar(text) != '\n')
          {
            svtkWPString_PushChar(text, '\n');
          }
          j = text->len;
          i = l;
        }

        /* keep paragraph breaks */
        else if (cp[l] == '\n')
        {
          i = l + 1;
          svtkWPString_Strip(text, "\n");
          if (text->len > 0)
          {
            svtkWPString_PushChar(text, '\n');
            svtkWPString_PushChar(text, '\n');
          }
          nojoin = 0;
          indent = 0;
          j = text->len;
          start = 1;
          continue;
        }

        /* add newline if nojoin is not set */
        else if (nojoin || (cp[i] == ' ' && !indent))
        {
          if (nojoin == 2)
          {
            nojoin = 0;
            indent = 0;
          }
          svtkWPString_PushChar(text, '\n');
          j = text->len;
        }

        /* do line joining */
        else if (text->len > 0 && svtkWPString_LastChar(text) != '\n')
        {
          i = l;
          svtkWPString_PushChar(text, ' ');
        }
      }

      /* handle quotes */
      if (cp[i] == '\"')
      {
        size_t q = i;
        size_t r = text->len;

        /* try to keep the quote whole */
        svtkWPString_PushChar(text, cp[i++]);
        while (cp[i] != '\"' && cp[i] != '\r' && cp[i] != '\n' && cp[i] != '\0')
        {
          svtkWPString_PushChar(text, cp[i++]);
        }
        /* if line ended before quote did, then reset */
        if (cp[i] != '\"')
        {
          i = q;
          text->len = r;
        }
      }
      else if (cp[i] == '\'')
      {
        size_t q = i;
        size_t r = text->len;

        /* try to keep the quote whole */
        svtkWPString_PushChar(text, cp[i++]);
        while (cp[i] != '\'' && cp[i] != '\r' && cp[i] != '\n' && cp[i] != '\0')
        {
          svtkWPString_PushChar(text, cp[i++]);
        }
        /* if line ended before quote did, then reset */
        if (cp[i] != '\'')
        {
          i = q;
          text->len = r;
        }
      }

      /* handle simple html tags */
      else if (cp[i] == '<')
      {
        l = i + 1;
        if (cp[l] == '/')
        {
          l++;
        }
        while ((cp[l] >= 'a' && cp[l] <= 'z') || (cp[l] >= 'A' && cp[l] <= 'Z'))
        {
          l++;
        }
        if (cp[l] == '>')
        {
          if (cp[i + 1] == 'p' || cp[i + 1] == 'P' || (cp[i + 1] == 'b' && cp[i + 2] == 'r') ||
            (cp[i + 1] == 'B' && cp[i + 2] == 'R'))
          {
            svtkWPString_Strip(text, " \n");
            svtkWPString_PushChar(text, '\n');
            svtkWPString_PushChar(text, '\n');
            j = text->len;
            indent = 0;
          }
          i = l + 1;
          while (cp[i] == '\r' || cp[i] == '\t' || cp[i] == ' ')
          {
            i++;
          }
        }
      }
      else if (cp[i] == '\\' || cp[i] == '@')
      {
        /* handle simple doxygen tags */
        if (strncmp(&cp[i + 1], "em ", 3) == 0)
        {
          i += 4;
        }
        else if (strncmp(&cp[i + 1], "a ", 2) == 0 || strncmp(&cp[i + 1], "e ", 2) == 0 ||
          strncmp(&cp[i + 1], "c ", 2) == 0 || strncmp(&cp[i + 1], "b ", 2) == 0 ||
          strncmp(&cp[i + 1], "p ", 2) == 0 || strncmp(&cp[i + 1], "f$", 2) == 0 ||
          strncmp(&cp[i + 1], "f[", 2) == 0 || strncmp(&cp[i + 1], "f]", 2) == 0)
        {
          if (i > 0 && cp[i - 1] != ' ')
          {
            svtkWPString_PushChar(text, ' ');
          }
          if (cp[i + 1] == 'f')
          {
            if (cp[i + 2] == '$')
            {
              svtkWPString_PushChar(text, '$');
            }
            else
            {
              svtkWPString_PushChar(text, '\\');
              svtkWPString_PushChar(text, cp[i + 2]);
            }
          }
          i += 3;
        }
        else if (cp[i + 1] == '&' || cp[i + 1] == '$' || cp[i + 1] == '#' || cp[i + 1] == '<' ||
          cp[i + 1] == '>' || cp[i + 1] == '%' || cp[i + 1] == '@' || cp[i + 1] == '\\' ||
          cp[i + 1] == '\"')
        {
          i++;
        }
        else if (cp[i + 1] == 'n')
        {
          svtkWPString_Strip(text, " \n");
          svtkWPString_PushChar(text, '\n');
          svtkWPString_PushChar(text, '\n');
          indent = 0;
          i += 2;
          j = text->len;
        }
        else if (strncmp(&cp[i + 1], "brief", 5) == 0)
        {
          i += 6;
          while (cp[i] == ' ' || cp[i] == '\r' || cp[i] == '\t')
          {
            i++;
          }
        }
        else if (strncmp(&cp[i + 1], "code", 4) == 0)
        {
          nojoin = 1;
          i += 5;
          while (cp[i] == ' ' || cp[i] == '\r' || cp[i] == '\t' || cp[i] == '\n')
          {
            i++;
          }
        }
        else if (strncmp(&cp[i + 1], "endcode", 7) == 0)
        {
          nojoin = 0;
          i += 8;
          l = i;
          while (cp[l] == ' ' || cp[l] == '\t' || cp[l] == '\r')
          {
            l++;
          }
          if (cp[l] == '\n')
          {
            i = l;
            svtkWPString_PushChar(text, '\n');
            j = text->len;
          }
        }
        else if (strncmp(&cp[i + 1], "verbatim", 8) == 0)
        {
          i += 9;
          while (cp[i] != '\0' &&
            ((cp[i] != '@' && cp[i] != '\\') || strncmp(&cp[i + 1], "endverbatim", 11) != 0))
          {
            if (cp[i] != '\r')
            {
              svtkWPString_PushChar(text, cp[i]);
            }
            if (cp[i] == '\n')
            {
              j = text->len;
            }
            i++;
          }
          if (cp[i] != '\0')
          {
            i += 12;
          }
        }
      }

      /* search for newline */
      start = 0;
      l = i;
      while (cp[l] == ' ' || cp[l] == '\t' || cp[l] == '\r')
      {
        l++;
      }
      if (cp[l] == '\n')
      {
        i = l + 1;
        start = 1;
      }

      /* append */
      else if (cp[i] != '\0')
      {
        svtkWPString_PushChar(text, cp[i++]);
      }

    } /* while (cp[i] != '\0' && text->len-j < width) */

    if (cp[i] == '\0')
    {
      break;
    }

    svtkWPString_BreakCommentLine(text, &j, indent);
  }

  /* remove any trailing blank lines */
  svtkWPString_Strip(text, "\n");
  svtkWPString_PushChar(text, '\n');

  return text->str;
}

/* -------------------------------------------------------------------- */
/* Create a signature for the python version of a method. */

static void svtkWrapText_PythonTypeSignature(
  struct svtkWPString* result, const char* delims[2], ValueInfo* arg);

static void svtkWrapText_PythonArraySignature(struct svtkWPString* result, const char* classname,
  const char* delims[2], int ndim, const char** dims);

const char* svtkWrapText_PythonSignature(FunctionInfo* currentFunction)
{
  /* string is intentionally not freed until the program exits */
  static struct svtkWPString staticString = { NULL, 0, 0 };
  struct svtkWPString* result;
  ValueInfo *arg, *ret;
  const char* parens[2] = { "(", ")" };
  const char* braces[2] = { "[", "]" };
  const char** delims;
  int i, n;

  n = svtkWrap_CountWrappedParameters(currentFunction);

  result = &staticString;
  result->len = 0;

  /* print out the name of the method */
  svtkWPString_Append(result, "V.");
  svtkWPString_Append(result, currentFunction->Name);

  /* print the arg list */
  svtkWPString_Append(result, "(");

  for (i = 0; i < n; i++)
  {
    arg = currentFunction->Parameters[i];

    if (i != 0)
    {
      svtkWPString_Append(result, ", ");
    }

    delims = parens;
    if (!svtkWrap_IsConst(arg) && !svtkWrap_IsSetVectorMethod(currentFunction))
    {
      delims = braces;
    }

    svtkWrapText_PythonTypeSignature(result, delims, arg);
  }

  svtkWPString_Append(result, ")");

  /* if this is a void method, we are finished */
  /* otherwise, print "->" and the return type */
  ret = currentFunction->ReturnValue;
  if (ret && (ret->Type & SVTK_PARSE_UNQUALIFIED_TYPE) != SVTK_PARSE_VOID)
  {
    svtkWPString_Append(result, " -> ");

    svtkWrapText_PythonTypeSignature(result, parens, ret);
  }

  if (currentFunction->Signature)
  {
    svtkWPString_Append(result, "\nC++: ");
    svtkWPString_Append(result, currentFunction->Signature);
  }

  return result->str;
}

static void svtkWrapText_PythonTypeSignature(
  struct svtkWPString* result, const char* braces[2], ValueInfo* arg)
{
  char text[256];
  const char* dimension;
  const char* classname = "";

  if (svtkWrap_IsVoid(arg))
  {
    classname = "void";
  }
  else if (svtkWrap_IsFunction(arg))
  {
    classname = "function";
  }
  else if (svtkWrap_IsString(arg) || svtkWrap_IsCharPointer(arg))
  {
    classname = "string";
    if ((arg->Type & SVTK_PARSE_BASE_TYPE) == SVTK_PARSE_UNICODE_STRING)
    {
      classname = "unicode";
    }
  }
  else if (svtkWrap_IsChar(arg))
  {
    classname = "char";
  }
  else if (svtkWrap_IsBool(arg))
  {
    classname = "bool";
  }
  else if (svtkWrap_IsRealNumber(arg))
  {
    classname = "float";
  }
  else if (svtkWrap_IsInteger(arg))
  {
    classname = "int";
  }
  else
  {
    svtkWrapText_PythonName(arg->Class, text);
    classname = text;
  }

  if ((svtkWrap_IsArray(arg) && arg->CountHint) || svtkWrap_IsPODPointer(arg))
  {
    svtkWPString_Append(result, braces[0]);
    svtkWPString_Append(result, classname);
    svtkWPString_Append(result, ", ...");
    svtkWPString_Append(result, braces[1]);
  }
  else if (svtkWrap_IsArray(arg))
  {
    sprintf(text, "%d", arg->Count);
    dimension = text;
    svtkWrapText_PythonArraySignature(result, classname, braces, 1, &dimension);
  }
  else if (svtkWrap_IsNArray(arg))
  {
    svtkWrapText_PythonArraySignature(
      result, classname, braces, arg->NumberOfDimensions, arg->Dimensions);
  }
  else
  {
    svtkWPString_Append(result, classname);
  }
}

static void svtkWrapText_PythonArraySignature(struct svtkWPString* result, const char* classname,
  const char* braces[2], int ndim, const char** dims)
{
  int j, n;

  svtkWPString_Append(result, braces[0]);
  n = (int)strtoul(dims[0], 0, 0);
  if (ndim > 1)
  {
    for (j = 0; j < n; j++)
    {
      if (j != 0)
      {
        svtkWPString_Append(result, ", ");
      }
      svtkWrapText_PythonArraySignature(result, classname, braces, ndim - 1, dims + 1);
    }
  }
  else
  {
    for (j = 0; j < n; j++)
    {
      if (j != 0)
      {
        svtkWPString_Append(result, ", ");
      }
      svtkWPString_Append(result, classname);
    }
  }
  svtkWPString_Append(result, braces[1]);
}

/* convert C++ identifier to a valid python identifier by mangling */
void svtkWrapText_PythonName(const char* name, char* pname)
{
  size_t j = 0;
  size_t i;
  size_t l;
  char* cp;
  int scoped = 0;

  /* look for first char that is not alphanumeric or underscore */
  l = svtkParse_IdentifierLength(name);

  if (name[l] != '\0')
  {
    /* get the mangled name */
    svtkParse_MangledTypeName(name, pname);

    /* put dots after namespaces */
    i = 0;
    cp = pname;
    if (cp[0] == 'S' && cp[1] >= 'a' && cp[1] <= 'z')
    {
      /* keep std:: namespace abbreviations */
      pname[j++] = *cp++;
      pname[j++] = *cp++;
    }
    while (*cp == 'N')
    {
      scoped++;
      cp++;
      while (*cp >= '0' && *cp <= '9')
      {
        i = i * 10 + (*cp++ - '0');
      }
      i += j;
      while (j < i)
      {
        pname[j++] = *cp++;
      }
      pname[j++] = '.';
    }

    /* remove mangling from first identifier and add an underscore */
    i = 0;
    while (*cp >= '0' && *cp <= '9')
    {
      i = i * 10 + (*cp++ - '0');
    }
    i += j;
    while (j < i)
    {
      pname[j++] = *cp++;
    }
    pname[j++] = '_';
    while (*cp != '\0')
    {
      pname[j++] = *cp++;
    }
    pname[j] = '\0';
  }
  else
  {
    strcpy(pname, name);
  }

  /* remove the "_E" that is added to mangled scoped names */
  if (scoped)
  {
    j = strlen(pname);
    if (j > 2 && pname[j - 2] == '_' && pname[j - 1] == 'E')
    {
      pname[j - 2] = '\0';
    }
  }
}