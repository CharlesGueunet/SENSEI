/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestThresholdTable.cxx

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

#include "svtkDoubleArray.h"
#include "svtkIntArray.h"
#include "svtkStringArray.h"
#include "svtkTable.h"
#include "svtkThresholdTable.h"
#include "svtkVariant.h"

#include "svtkSmartPointer.h"
#define SVTK_CREATE(type, name) svtkSmartPointer<type> name = svtkSmartPointer<type>::New()

int TestThresholdTable(int svtkNotUsed(argc), char* svtkNotUsed(argv)[])
{
  // Create the test input
  SVTK_CREATE(svtkTable, table);
  SVTK_CREATE(svtkIntArray, intArr);
  intArr->SetName("intArr");
  intArr->InsertNextValue(0);
  intArr->InsertNextValue(1);
  intArr->InsertNextValue(2);
  intArr->InsertNextValue(3);
  intArr->InsertNextValue(4);
  table->AddColumn(intArr);
  SVTK_CREATE(svtkDoubleArray, doubleArr);
  doubleArr->SetName("doubleArr");
  doubleArr->InsertNextValue(1.0);
  doubleArr->InsertNextValue(1.1);
  doubleArr->InsertNextValue(1.2);
  doubleArr->InsertNextValue(1.3);
  doubleArr->InsertNextValue(1.4);
  table->AddColumn(doubleArr);
  SVTK_CREATE(svtkStringArray, stringArr);
  stringArr->SetName("stringArr");
  stringArr->InsertNextValue("10");
  stringArr->InsertNextValue("11");
  stringArr->InsertNextValue("12");
  stringArr->InsertNextValue("13");
  stringArr->InsertNextValue("14");
  table->AddColumn(stringArr);

  // Use the ThresholdTable
  SVTK_CREATE(svtkThresholdTable, threshold);
  threshold->SetInputData(table);

  int errors = 0;
  threshold->SetInputArrayToProcess(0, 0, 0, svtkDataObject::FIELD_ASSOCIATION_ROWS, "intArr");
  threshold->SetMinValue(svtkVariant(3));
  threshold->SetMaxValue(svtkVariant(5));
  threshold->SetMode(svtkThresholdTable::ACCEPT_BETWEEN);
  threshold->Update();
  svtkTable* output = threshold->GetOutput();
  svtkIntArray* intArrOut = svtkArrayDownCast<svtkIntArray>(output->GetColumnByName("intArr"));

  // Perform error checking
  if (!intArrOut)
  {
    cerr << "int array undefined in output" << endl;
    errors++;
  }
  else if (intArrOut->GetNumberOfTuples() != 2)
  {
    cerr << "int threshold should have 2 tuples, instead has " << intArrOut->GetNumberOfTuples()
         << endl;
    errors++;
  }
  else
  {
    if (intArrOut->GetValue(0) != 3)
    {
      cerr << "int array [0] should be 3 but is " << intArrOut->GetValue(0) << endl;
      errors++;
    }
    if (intArrOut->GetValue(1) != 4)
    {
      cerr << "int array [1] should be 4 but is " << intArrOut->GetValue(1) << endl;
      errors++;
    }
  }

  threshold->SetInputArrayToProcess(0, 0, 0, svtkDataObject::FIELD_ASSOCIATION_ROWS, "doubleArr");
  threshold->SetMaxValue(svtkVariant(1.2));
  threshold->SetMode(svtkThresholdTable::ACCEPT_LESS_THAN);
  threshold->Update();
  output = threshold->GetOutput();
  svtkDoubleArray* doubleArrOut =
    svtkArrayDownCast<svtkDoubleArray>(output->GetColumnByName("doubleArr"));

  // Perform error checking
  if (!doubleArrOut)
  {
    cerr << "double array undefined in output" << endl;
    errors++;
  }
  else if (doubleArrOut->GetNumberOfTuples() != 3)
  {
    cerr << "double threshold should have 3 tuples, instead has " << intArrOut->GetNumberOfTuples()
         << endl;
    errors++;
  }
  else
  {
    if (doubleArrOut->GetValue(0) != 1.0)
    {
      cerr << "double array [0] should be 1.0 but is " << doubleArrOut->GetValue(0) << endl;
      errors++;
    }
    if (doubleArrOut->GetValue(1) != 1.1)
    {
      cerr << "double array [1] should be 1.1 but is " << doubleArrOut->GetValue(1) << endl;
      errors++;
    }
    if (doubleArrOut->GetValue(2) != 1.2)
    {
      cerr << "double array [2] should be 1.2 but is " << doubleArrOut->GetValue(2) << endl;
      errors++;
    }
  }

  threshold->SetInputArrayToProcess(0, 0, 0, svtkDataObject::FIELD_ASSOCIATION_ROWS, "stringArr");
  threshold->SetMinValue(svtkVariant("10"));
  threshold->SetMaxValue(svtkVariant("13"));
  threshold->SetMode(svtkThresholdTable::ACCEPT_OUTSIDE);
  threshold->Update();
  output = threshold->GetOutput();
  svtkStringArray* stringArrOut =
    svtkArrayDownCast<svtkStringArray>(output->GetColumnByName("stringArr"));

  // Perform error checking
  if (!stringArrOut)
  {
    cerr << "string array undefined in output" << endl;
    errors++;
  }
  else if (stringArrOut->GetNumberOfTuples() != 3)
  {
    cerr << "string threshold should have 3 tuples, instead has "
         << stringArrOut->GetNumberOfTuples() << endl;
    errors++;
  }
  else
  {
    if (stringArrOut->GetValue(0) != "10")
    {
      cerr << "string array [0] should be 10 but is " << stringArrOut->GetValue(0) << endl;
      errors++;
    }
    if (stringArrOut->GetValue(1) != "13")
    {
      cerr << "string array [1] should be 13 but is " << stringArrOut->GetValue(1) << endl;
      errors++;
    }
    if (stringArrOut->GetValue(2) != "14")
    {
      cerr << "string array [2] should be 14 but is " << stringArrOut->GetValue(2) << endl;
      errors++;
    }
  }

  return errors;
}