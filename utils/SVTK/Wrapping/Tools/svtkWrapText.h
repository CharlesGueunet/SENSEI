/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkWrapText.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/**
 * svtkWrap provides useful functions for generating wrapping code.
 */

#ifndef svtkWrapText_h
#define svtkWrapText_h

#include "svtkParse.h"
#include "svtkParseHierarchy.h"
#include "svtkWrappingToolsModule.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * Quote a string for inclusion in a C string literal.  The "maxlen"
   * should be set to a value between 32 and 2047.  Values over 2047
   * will result in string literals too long for some compilers.  If
   * the string is truncated, a "..." will be appended.
   */
  SVTKWRAPPINGTOOLS_EXPORT const char* svtkWrapText_QuoteString(const char* comment, size_t maxlen);

  /**
   * Format a doxygen comment for plain text, and word-wrap at
   * the specified width.  A 70-char width is recommended.
   */
  SVTKWRAPPINGTOOLS_EXPORT const char* svtkWrapText_FormatComment(const char* comment, size_t width);

  /**
   * Format a method signature by applying word-wrap at the specified
   * width and taking special care not to split any literals or names.
   * A width of 70 chars is recommended.
   */
  SVTKWRAPPINGTOOLS_EXPORT const char* svtkWrapText_FormatSignature(
    const char* signature, size_t width, size_t maxlen);

  /**
   * Produce a python signature for a method, for use in documentation.
   */
  SVTKWRAPPINGTOOLS_EXPORT const char* svtkWrapText_PythonSignature(FunctionInfo* currentFunction);

  /**
   * Convert a C++ identifier into an identifier that can be used from Python.
   * The "::" namespace separators are converted to ".", and template args
   * are mangled and prefix with "T" according to the ia64 ABI. The output
   * parameter "pname" must be large enough to accept the result.  If it is
   * as long as the input name, that is sufficient. */
  SVTKWRAPPINGTOOLS_EXPORT void svtkWrapText_PythonName(const char* name, char* pname);

#ifdef __cplusplus
}
#endif

#endif
/* SVTK-HeaderTest-Exclude: svtkWrapText.h */