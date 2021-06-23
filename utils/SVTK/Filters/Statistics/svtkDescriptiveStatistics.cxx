/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkDescriptiveStatistics.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*-------------------------------------------------------------------------
  Copyright 2011 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

#include "svtkToolkits.h"

#include "svtkDescriptiveStatistics.h"
#include "svtkStatisticsAlgorithmPrivate.h"

#include "svtkDataObjectCollection.h"
#include "svtkDoubleArray.h"
#include "svtkIdTypeArray.h"
#include "svtkInformation.h"
#include "svtkMath.h"
#include "svtkMultiBlockDataSet.h"
#include "svtkObjectFactory.h"
#include "svtkStdString.h"
#include "svtkStringArray.h"
#include "svtkTable.h"
#include "svtkVariantArray.h"

#include <limits>
#include <set>
#include <sstream>
#include <vector>

svtkObjectFactoryNewMacro(svtkDescriptiveStatistics);

// ----------------------------------------------------------------------
svtkDescriptiveStatistics::svtkDescriptiveStatistics()
{
  this->AssessNames->SetNumberOfValues(1);
  this->AssessNames->SetValue(
    0, "d"); // relative deviation, i.e., when unsigned, 1D Mahalanobis distance

  this->UnbiasedVariance =
    1; // By default, use unbiased estimator of the variance (divided by cardinality-1)
  this->G1Skewness = 0;       // By default, use g1 estimator of the skewness (G1 otherwise)
  this->G2Kurtosis = 0;       // By default, use g2 estimator of the kurtosis (G2 otherwise)
  this->SignedDeviations = 0; // By default, use unsigned deviation (1D Mahlanobis distance)
}

// ----------------------------------------------------------------------
svtkDescriptiveStatistics::~svtkDescriptiveStatistics() = default;

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "UnbiasedVariance: " << this->UnbiasedVariance << "\n";
  os << indent << "G1Skewness: " << this->G1Skewness << "\n";
  os << indent << "G2Kurtosis: " << this->G2Kurtosis << "\n";
  os << indent << "SignedDeviations: " << this->SignedDeviations << "\n";
}

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::Aggregate(
  svtkDataObjectCollection* inMetaColl, svtkMultiBlockDataSet* outMeta)
{
  if (!outMeta)
  {
    return;
  }

  // Get hold of the first model (data object) in the collection
  svtkCollectionSimpleIterator it;
  inMetaColl->InitTraversal(it);
  svtkDataObject* inMetaDO = inMetaColl->GetNextDataObject(it);

  // Verify that the first input model is indeed contained in a multiblock data set
  svtkMultiBlockDataSet* inMeta = svtkMultiBlockDataSet::SafeDownCast(inMetaDO);
  if (!inMeta)
  {
    return;
  }

  // Verify that the first primary statistics are indeed contained in a table
  svtkTable* primaryTab = svtkTable::SafeDownCast(inMeta->GetBlock(0));
  if (!primaryTab)
  {
    return;
  }

  svtkIdType nRow = primaryTab->GetNumberOfRows();
  if (!nRow)
  {
    // No statistics were calculated.
    return;
  }

  // Use this first model to initialize the aggregated one
  svtkTable* aggregatedTab = svtkTable::New();
  aggregatedTab->DeepCopy(primaryTab);

  // Now, loop over all remaining models and update aggregated each time
  while ((inMetaDO = inMetaColl->GetNextDataObject(it)))
  {
    // Verify that the current model is indeed contained in a multiblock data set
    inMeta = svtkMultiBlockDataSet::SafeDownCast(inMetaDO);
    if (!inMeta)
    {
      aggregatedTab->Delete();

      return;
    }

    // Verify that the current primary statistics are indeed contained in a table
    primaryTab = svtkTable::SafeDownCast(inMeta->GetBlock(0));
    if (!primaryTab)
    {
      aggregatedTab->Delete();

      return;
    }

    if (primaryTab->GetNumberOfRows() != nRow)
    {
      // Models do not match
      aggregatedTab->Delete();

      return;
    }

    // Iterate over all model rows
    for (int r = 0; r < nRow; ++r)
    {
      // Verify that variable names match each other
      if (primaryTab->GetValueByName(r, "Variable") != aggregatedTab->GetValueByName(r, "Variable"))
      {
        // Models do not match
        aggregatedTab->Delete();

        return;
      }

      // Get aggregated statistics
      int n = aggregatedTab->GetValueByName(r, "Cardinality").ToInt();
      double min = aggregatedTab->GetValueByName(r, "Minimum").ToDouble();
      double max = aggregatedTab->GetValueByName(r, "Maximum").ToDouble();
      double mean = aggregatedTab->GetValueByName(r, "Mean").ToDouble();
      double M2 = aggregatedTab->GetValueByName(r, "M2").ToDouble();
      double M3 = aggregatedTab->GetValueByName(r, "M3").ToDouble();
      double M4 = aggregatedTab->GetValueByName(r, "M4").ToDouble();

      // Get current model statistics
      int n_c = primaryTab->GetValueByName(r, "Cardinality").ToInt();
      double min_c = primaryTab->GetValueByName(r, "Minimum").ToDouble();
      double max_c = primaryTab->GetValueByName(r, "Maximum").ToDouble();
      double mean_c = primaryTab->GetValueByName(r, "Mean").ToDouble();
      double M2_c = primaryTab->GetValueByName(r, "M2").ToDouble();
      double M3_c = primaryTab->GetValueByName(r, "M3").ToDouble();
      double M4_c = primaryTab->GetValueByName(r, "M4").ToDouble();

      // Update global statics
      int N = n + n_c;

      if (min_c < min)
      {
        aggregatedTab->SetValueByName(r, "Minimum", min_c);
      }

      if (max_c > max)
      {
        aggregatedTab->SetValueByName(r, "Maximum", max_c);
      }

      double delta = mean_c - mean;
      double delta_sur_N = delta / static_cast<double>(N);
      double delta2_sur_N2 = delta_sur_N * delta_sur_N;

      int n2 = n * n;
      int n_c2 = n_c * n_c;
      int prod_n = n * n_c;

      M4 += M4_c + prod_n * (n2 - prod_n + n_c2) * delta * delta_sur_N * delta2_sur_N2 +
        6. * (n2 * M2_c + n_c2 * M2) * delta2_sur_N2 + 4. * (n * M3_c - n_c * M3) * delta_sur_N;

      M3 += M3_c + prod_n * (n - n_c) * delta * delta2_sur_N2 +
        3. * (n * M2_c - n_c * M2) * delta_sur_N;

      M2 += M2_c + prod_n * delta * delta_sur_N;

      mean += n_c * delta_sur_N;

      // Store updated model
      aggregatedTab->SetValueByName(r, "Cardinality", N);
      aggregatedTab->SetValueByName(r, "Mean", mean);
      aggregatedTab->SetValueByName(r, "M2", M2);
      aggregatedTab->SetValueByName(r, "M3", M3);
      aggregatedTab->SetValueByName(r, "M4", M4);
    }
  }

  // Finally set first block of aggregated model to primary statistics table
  outMeta->SetNumberOfBlocks(1);
  outMeta->GetMetaData(static_cast<unsigned>(0))
    ->Set(svtkCompositeDataSet::NAME(), "Primary Statistics");
  outMeta->SetBlock(0, aggregatedTab);

  // Clean up
  aggregatedTab->Delete();
}

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::Learn(
  svtkTable* inData, svtkTable* svtkNotUsed(inParameters), svtkMultiBlockDataSet* outMeta)
{
  if (!inData)
  {
    return;
  }

  if (!outMeta)
  {
    return;
  }

  // The primary statistics table
  svtkTable* primaryTab = svtkTable::New();

  svtkStringArray* stringCol = svtkStringArray::New();
  stringCol->SetName("Variable");
  primaryTab->AddColumn(stringCol);
  stringCol->Delete();

  svtkIdTypeArray* idTypeCol = svtkIdTypeArray::New();
  idTypeCol->SetName("Cardinality");
  primaryTab->AddColumn(idTypeCol);
  idTypeCol->Delete();

  svtkDoubleArray* doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("Minimum");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("Maximum");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("Mean");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("M2");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("M3");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  doubleCol = svtkDoubleArray::New();
  doubleCol->SetName("M4");
  primaryTab->AddColumn(doubleCol);
  doubleCol->Delete();

  // Loop over requests
  svtkIdType nRow = inData->GetNumberOfRows();
  for (std::set<std::set<svtkStdString> >::const_iterator rit = this->Internals->Requests.begin();
       rit != this->Internals->Requests.end(); ++rit)
  {
    // Each request contains only one column of interest (if there are others, they are ignored)
    std::set<svtkStdString>::const_iterator it = rit->begin();
    svtkStdString varName = *it;
    if (!inData->GetColumnByName(varName))
    {
      svtkWarningMacro(
        "InData table does not have a column " << varName.c_str() << ". Ignoring it.");
      continue;
    }

    double minVal = inData->GetValueByName(0, varName).ToDouble();
    double maxVal = minVal;
    double mean = 0.;
    double mom2 = 0.;
    double mom3 = 0.;
    double mom4 = 0.;

    double n, inv_n, val, delta, A, B;
    for (svtkIdType r = 0; r < nRow; ++r)
    {
      n = r + 1.;
      inv_n = 1. / n;

      val = inData->GetValueByName(r, varName).ToDouble();
      delta = val - mean;

      A = delta * inv_n;
      mean += A;
      mom4 += A * (A * A * delta * r * (n * (n - 3.) + 3.) + 6. * A * mom2 - 4. * mom3);

      B = val - mean;
      mom3 += A * (B * delta * (n - 2.) - 3. * mom2);
      mom2 += delta * B;

      if (val < minVal)
      {
        minVal = val;
      }
      else if (val > maxVal)
      {
        maxVal = val;
      }
    }

    svtkVariantArray* row = svtkVariantArray::New();

    row->SetNumberOfValues(8);

    row->SetValue(0, varName);
    row->SetValue(1, nRow);
    row->SetValue(2, minVal);
    row->SetValue(3, maxVal);
    row->SetValue(4, mean);
    row->SetValue(5, mom2);
    row->SetValue(6, mom3);
    row->SetValue(7, mom4);

    primaryTab->InsertNextRow(row);

    row->Delete();
  } // rit

  // Finally set first block of output meta port to primary statistics table
  outMeta->SetNumberOfBlocks(1);
  outMeta->GetMetaData(static_cast<unsigned>(0))
    ->Set(svtkCompositeDataSet::NAME(), "Primary Statistics");
  outMeta->SetBlock(0, primaryTab);

  // Clean up
  primaryTab->Delete();
}

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::Derive(svtkMultiBlockDataSet* inMeta)
{
  if (!inMeta || inMeta->GetNumberOfBlocks() < 1)
  {
    return;
  }

  svtkTable* primaryTab = svtkTable::SafeDownCast(inMeta->GetBlock(0));
  if (!primaryTab)
  {
    return;
  }

  int numDoubles = 5;
  svtkStdString doubleNames[] = { "Standard Deviation", "Variance", "Skewness", "Kurtosis", "Sum" };

  // Create table for derived statistics
  svtkIdType nRow = primaryTab->GetNumberOfRows();
  svtkTable* derivedTab = svtkTable::New();
  svtkDoubleArray* doubleCol;
  for (int j = 0; j < numDoubles; ++j)
  {
    if (!derivedTab->GetColumnByName(doubleNames[j]))
    {
      doubleCol = svtkDoubleArray::New();
      doubleCol->SetName(doubleNames[j]);
      doubleCol->SetNumberOfTuples(nRow);
      derivedTab->AddColumn(doubleCol);
      doubleCol->Delete();
    }
  }

  // Storage for standard deviation, variance, skewness,  kurtosis, sum
  std::vector<double> derivedVals(numDoubles);

  for (int i = 0; i < nRow; ++i)
  {
    double mom2 = primaryTab->GetValueByName(i, "M2").ToDouble();
    double mom3 = primaryTab->GetValueByName(i, "M3").ToDouble();
    double mom4 = primaryTab->GetValueByName(i, "M4").ToDouble();

    int numSamples = primaryTab->GetValueByName(i, "Cardinality").ToInt();

    if (numSamples == 1 || mom2 < 1.e-150)
    {
      derivedVals[0] = 0.;
      derivedVals[1] = 0.;
      derivedVals[2] = 0.;
      derivedVals[3] = 0.;
      derivedVals[4] = 0.;
    }
    else
    {
      double n = static_cast<double>(numSamples);
      double inv_n = 1. / n;
      double nm1 = n - 1.;

      // Variance
      if (this->UnbiasedVariance)
      {
        derivedVals[1] = mom2 / nm1;
      }
      else // use population variance
      {
        derivedVals[1] = mom2 * inv_n;
      }

      // Standard deviation
      derivedVals[0] = sqrt(derivedVals[1]);

      // Skeweness and kurtosis
      double var_inv = nm1 / mom2;
      double nvar_inv = var_inv * inv_n;
      derivedVals[2] = nvar_inv * sqrt(var_inv) * mom3;
      derivedVals[3] = nvar_inv * var_inv * mom4 - 3.;

      if (this->G1Skewness && n > 2)
      {
        // G1 skewness estimate
        derivedVals[2] *= (n * n) / (nm1 * (nm1 - 1.));
      }

      if (this->G2Kurtosis && n > 3)
      {
        // G2 kurtosis estimate
        derivedVals[3] *= ((n + 1.) * derivedVals[4] + 6.) * nm1 / ((nm1 - 1.) * (nm1 - 2.));
      }
    }

    // Sum
    derivedVals[4] = numSamples * primaryTab->GetValueByName(i, "Mean").ToDouble();

    for (int j = 0; j < numDoubles; ++j)
    {
      derivedTab->SetValueByName(i, doubleNames[j], derivedVals[j]);
    }
  }

  // Finally set second block of output meta port to derived statistics table
  inMeta->SetNumberOfBlocks(2);
  inMeta->GetMetaData(static_cast<unsigned>(1))
    ->Set(svtkCompositeDataSet::NAME(), "Derived Statistics");
  inMeta->SetBlock(1, derivedTab);

  // Clean up
  derivedTab->Delete();
}

// ----------------------------------------------------------------------
// Use the invalid value of -1 for p-values if R is absent
svtkDoubleArray* svtkDescriptiveStatistics::CalculatePValues(svtkDoubleArray* statCol)
{
  // A column must be created first
  svtkDoubleArray* testCol = svtkDoubleArray::New();

  // Fill this column
  svtkIdType n = statCol->GetNumberOfTuples();
  testCol->SetNumberOfTuples(n);
  for (svtkIdType r = 0; r < n; ++r)
  {
    testCol->SetTuple1(r, -1);
  }

  return testCol;
}

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::Test(
  svtkTable* inData, svtkMultiBlockDataSet* inMeta, svtkTable* outMeta)
{
  if (!inMeta)
  {
    return;
  }

  svtkTable* primaryTab = svtkTable::SafeDownCast(inMeta->GetBlock(0));
  if (!primaryTab)
  {
    return;
  }

  svtkTable* derivedTab = svtkTable::SafeDownCast(inMeta->GetBlock(1));
  if (!derivedTab)
  {
    return;
  }

  svtkIdType nRowPrim = primaryTab->GetNumberOfRows();
  if (nRowPrim != derivedTab->GetNumberOfRows())
  {
    svtkErrorMacro("Inconsistent input: primary model has "
      << nRowPrim << " rows but derived model has " << derivedTab->GetNumberOfRows()
      << ". Cannot test.");
    return;
  }

  if (!outMeta)
  {
    return;
  }

  // Prepare columns for the test:
  // 0: variable name
  // 1: Jarque-Bera statistic
  // 2: Jarque-Bera p-value (calculated only if R is available, filled with -1 otherwise)
  // NB: These are not added to the output table yet, for they will be filled individually first
  //     in order that R be invoked only once.
  svtkStringArray* nameCol = svtkStringArray::New();
  nameCol->SetName("Variable");

  svtkDoubleArray* statCol = svtkDoubleArray::New();
  statCol->SetName("Jarque-Bera");

  // Downcast columns to string arrays for efficient data access
  svtkStringArray* vars = svtkArrayDownCast<svtkStringArray>(primaryTab->GetColumnByName("Variable"));

  // Loop over requests
  for (std::set<std::set<svtkStdString> >::const_iterator rit = this->Internals->Requests.begin();
       rit != this->Internals->Requests.end(); ++rit)
  {
    // Each request contains only one column of interest (if there are others, they are ignored)
    std::set<svtkStdString>::const_iterator it = rit->begin();
    svtkStdString varName = *it;
    if (!inData->GetColumnByName(varName))
    {
      svtkWarningMacro(
        "InData table does not have a column " << varName.c_str() << ". Ignoring it.");
      continue;
    }

    // Find the model row that corresponds to the variable of the request
    svtkIdType r = 0;
    while (r < nRowPrim && vars->GetValue(r) != varName)
    {
      ++r;
    }
    if (r >= nRowPrim)
    {
      svtkWarningMacro(
        "Incomplete input: model does not have a row " << varName.c_str() << ". Cannot test.");
      continue;
    }

    // Retrieve model statistics necessary for Jarque-Bera testing
    double n = primaryTab->GetValueByName(r, "Cardinality").ToDouble();
    double skew = derivedTab->GetValueByName(r, "Skewness").ToDouble();
    double kurt = derivedTab->GetValueByName(r, "Kurtosis").ToDouble();

    // Now calculate Jarque-Bera statistic
    double jb = n * (skew * skew + .25 * kurt * kurt) / 6.;

    // Insert variable name and calculated Jarque-Bera statistic
    // NB: R will be invoked only once at the end for efficiency
    nameCol->InsertNextValue(varName);
    statCol->InsertNextTuple1(jb);
  } // rit

  // Now, add the already prepared columns to the output table
  outMeta->AddColumn(nameCol);
  outMeta->AddColumn(statCol);

  // Last phase: compute the p-values or assign invalid value if they cannot be computed
  // If available, use R to obtain the p-values for the Chi square distribution with 2 DOFs
  svtkDoubleArray* testCol = this->CalculatePValues(statCol);

  // The test column name can only be set after the column has been obtained from R
  testCol->SetName("P");

  // Now add the column of invalid values to the output table
  outMeta->AddColumn(testCol);

  testCol->Delete();

  // Clean up
  nameCol->Delete();
  statCol->Delete();
}

// ----------------------------------------------------------------------
class TableColumnDeviantFunctor : public svtkStatisticsAlgorithm::AssessFunctor
{
public:
  svtkDataArray* Data;
  double Nominal;
  double Deviation;
};

// When the deviation is 0, we can't normalize. Instead, a non-zero value (1)
// is returned only when the nominal value is matched exactly.
class ZedDeviationDeviantFunctor : public TableColumnDeviantFunctor
{
public:
  ZedDeviationDeviantFunctor(svtkDataArray* vals, double nominal)
  {
    this->Data = vals;
    this->Nominal = nominal;
  }
  ~ZedDeviationDeviantFunctor() override = default;
  void operator()(svtkDoubleArray* result, svtkIdType id) override
  {
    result->SetNumberOfValues(1);
    result->SetValue(0, (this->Data->GetTuple1(id) == this->Nominal) ? 0. : 1.);
  }
};

class SignedTableColumnDeviantFunctor : public TableColumnDeviantFunctor
{
public:
  SignedTableColumnDeviantFunctor(svtkDataArray* vals, double nominal, double deviation)
  {
    this->Data = vals;
    this->Nominal = nominal;
    this->Deviation = deviation;
  }
  ~SignedTableColumnDeviantFunctor() override = default;
  void operator()(svtkDoubleArray* result, svtkIdType id) override
  {
    result->SetNumberOfValues(1);
    result->SetValue(0, (this->Data->GetTuple1(id) - this->Nominal) / this->Deviation);
  }
};

class UnsignedTableColumnDeviantFunctor : public TableColumnDeviantFunctor
{
public:
  UnsignedTableColumnDeviantFunctor(svtkDataArray* vals, double nominal, double deviation)
  {
    this->Data = vals;
    this->Nominal = nominal;
    this->Deviation = deviation;
  }
  ~UnsignedTableColumnDeviantFunctor() override = default;
  void operator()(svtkDoubleArray* result, svtkIdType id) override
  {
    result->SetNumberOfValues(1);
    result->SetValue(0, fabs(this->Data->GetTuple1(id) - this->Nominal) / this->Deviation);
  }
};

// ----------------------------------------------------------------------
void svtkDescriptiveStatistics::SelectAssessFunctor(
  svtkTable* outData, svtkDataObject* inMetaDO, svtkStringArray* rowNames, AssessFunctor*& dfunc)
{
  dfunc = nullptr;
  svtkMultiBlockDataSet* inMeta = svtkMultiBlockDataSet::SafeDownCast(inMetaDO);
  if (!inMeta)
  {
    return;
  }

  svtkTable* primaryTab = svtkTable::SafeDownCast(inMeta->GetBlock(0));
  if (!primaryTab)
  {
    return;
  }

  svtkTable* derivedTab = svtkTable::SafeDownCast(inMeta->GetBlock(1));
  if (!derivedTab)
  {
    return;
  }

  svtkIdType nRowPrim = primaryTab->GetNumberOfRows();
  if (nRowPrim != derivedTab->GetNumberOfRows())
  {
    return;
  }

  svtkStdString varName = rowNames->GetValue(0);

  // Downcast meta columns to string arrays for efficient data access
  svtkStringArray* vars = svtkArrayDownCast<svtkStringArray>(primaryTab->GetColumnByName("Variable"));
  if (!vars)
  {
    return;
  }

  // Loop over primary statistics table until the requested variable is found
  for (int r = 0; r < nRowPrim; ++r)
  {
    if (vars->GetValue(r) == varName)
    {
      // Grab the data for the requested variable
      svtkAbstractArray* arr = outData->GetColumnByName(varName);
      if (!arr)
      {
        return;
      }

      // For descriptive statistics, type must be convertible to DataArray
      // E.g., StringArrays do not fit here
      svtkDataArray* vals = svtkArrayDownCast<svtkDataArray>(arr);
      if (!vals)
      {
        return;
      }

      // Fetch necessary value from primary model
      double mean = primaryTab->GetValueByName(r, "Mean").ToDouble();

      // Fetch necessary value from derived model
      double stdv = derivedTab->GetValueByName(r, "Standard Deviation").ToDouble();
      // NB: If derived values were specified (and not calculated by Derive)
      //     and are inconsistent, then incorrect assessments will be produced

      if (stdv < SVTK_DBL_MIN)
      {
        dfunc = new ZedDeviationDeviantFunctor(vals, mean);
      }
      else
      {
        if (this->GetSignedDeviations())
        {
          dfunc = new SignedTableColumnDeviantFunctor(vals, mean, stdv);
        }
        else
        {
          dfunc = new UnsignedTableColumnDeviantFunctor(vals, mean, stdv);
        }
      }

      return;
    }
  }

  // If arrived here it means that the variable of interest was not found in the parameter table
}