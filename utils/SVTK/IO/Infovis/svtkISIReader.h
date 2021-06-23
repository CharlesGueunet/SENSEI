/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkISIReader.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/
/**
 * @class   svtkISIReader
 * @brief   reader for ISI files
 *
 *
 * ISI is a tagged format for expressing bibliographic citations.  Data is
 * structured as a collection of records with each record composed of
 * one-to-many fields.  See
 *
 * http://isibasic.com/help/helpprn.html#dialog_export_format
 *
 * for details.  svtkISIReader will convert an ISI file into a svtkTable, with
 * the set of table columns determined dynamically from the contents of the
 * file.
 */

#ifndef svtkISIReader_h
#define svtkISIReader_h

#include "svtkIOInfovisModule.h" // For export macro
#include "svtkTableAlgorithm.h"

class svtkTable;

class SVTKIOINFOVIS_EXPORT svtkISIReader : public svtkTableAlgorithm
{
public:
  static svtkISIReader* New();
  svtkTypeMacro(svtkISIReader, svtkTableAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Set/get the file to load
   */
  svtkGetStringMacro(FileName);
  svtkSetStringMacro(FileName);
  //@}

  //@{
  /**
   * Set/get the delimiter to be used for concatenating field data (default: ";")
   */
  svtkGetStringMacro(Delimiter);
  svtkSetStringMacro(Delimiter);
  //@}

  //@{
  /**
   * Set/get the maximum number of records to read from the file (zero = unlimited)
   */
  svtkGetMacro(MaxRecords, int);
  svtkSetMacro(MaxRecords, int);
  //@}

protected:
  svtkISIReader();
  ~svtkISIReader() override;

  int RequestData(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;

  char* FileName;
  char* Delimiter;
  int MaxRecords;

private:
  svtkISIReader(const svtkISIReader&) = delete;
  void operator=(const svtkISIReader&) = delete;
};

#endif