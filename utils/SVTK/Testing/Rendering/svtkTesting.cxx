/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkTesting.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkTesting.h"

#include "svtkDataArray.h"
#include "svtkDataSet.h"
#include "svtkDoubleArray.h"
#include "svtkFloatArray.h"
#include "svtkImageClip.h"
#include "svtkImageData.h"
#include "svtkImageDifference.h"
#include "svtkImageExtractComponents.h"
#include "svtkImageShiftScale.h"
#include "svtkInformation.h"
#include "svtkInteractorEventRecorder.h"
#include "svtkNew.h"
#include "svtkObjectFactory.h"
#include "svtkPNGReader.h"
#include "svtkPNGWriter.h"
#include "svtkPointData.h"
#include "svtkPointSet.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkTimerLog.h"
#include "svtkToolkits.h"
#include "svtkWindowToImageFilter.h"

#include <sstream>
#include <svtksys/SystemTools.hxx>

svtkStandardNewMacro(svtkTesting);
svtkCxxSetObjectMacro(svtkTesting, RenderWindow, svtkRenderWindow);

using std::string;
using std::vector;

//-----------------------------------------------------------------------------
// Find in command tail, failing that find in environment,
// failing that return a default.
// Up to caller to delete the string returned.
static string svtkTestingGetArgOrEnvOrDefault(
  const string& argName, // argument idnetifier flag. eg "-D"
  vector<string>& argv,  // command tail
  const string& env,     // environment variable name to find
  const string& def)     // default to use if "env" is not found.
{
  string argValue;

  // Search command tail.
  int argc = static_cast<int>(argv.size());
  for (int i = 0; i < argc; i++)
  {
    if ((i < (argc - 1)) && (argName == argv[i]))
    {
      argValue = argv[i + 1];
    }
  }
  // If not found search environment.
  if (argValue.empty() && !(env.empty() || def.empty()))
  {
    char* foundenv = getenv(env.c_str());
    if (foundenv)
    {
      argValue = foundenv;
    }
    else
    {
      // Not found, fall back to default.
      argValue = def;
    }
  }

  return argValue;
}

//-----------------------------------------------------------------------------
// Description:
// Sum the L2 Norm point wise over all tuples. Each term
// is scaled by the magnitude of one of the inputs.
// Return sum and the number of terms.
template <class T>
svtkIdType AccumulateScaledL2Norm(T* pA, // pointer to first data array
  T* pB,                                // pointer to second data array
  svtkIdType nTups,                      // number of tuples
  int nComps,                           // number of comps
  double& SumModR)                      // result
{
  //
  SumModR = 0.0;
  for (svtkIdType i = 0; i < nTups; ++i)
  {
    double modR = 0.0;
    double modA = 0.0;
    for (int q = 0; q < nComps; ++q)
    {
      double a = pA[q];
      double b = pB[q];
      modA += a * a;
      double r = b - a;
      modR += r * r;
    }
    modA = sqrt(modA);
    modA = modA < 1.0 ? 1.0 : modA;
    SumModR += sqrt(modR) / modA;
    pA += nComps;
    pB += nComps;
  }
  return nTups;
}

//=============================================================================
svtkTesting::svtkTesting()
{
  this->FrontBuffer = 0;
  this->RenderWindow = nullptr;
  this->ValidImageFileName = nullptr;
  this->ImageDifference = 0;
  this->DataRoot = nullptr;
  this->TempDirectory = nullptr;
  this->BorderOffset = 0;
  this->Verbose = 0;

  // on construction we start the timer
  this->StartCPUTime = svtkTimerLog::GetCPUTime();
  this->StartWallTime = svtkTimerLog::GetUniversalTime();
}

//-----------------------------------------------------------------------------
svtkTesting::~svtkTesting()
{
  this->SetRenderWindow(nullptr);
  this->SetValidImageFileName(nullptr);
  this->SetDataRoot(nullptr);
  this->SetTempDirectory(nullptr);
}

//-----------------------------------------------------------------------------
void svtkTesting::AddArgument(const char* arg)
{
  this->Args.push_back(arg);
}

//-----------------------------------------------------------------------------
void svtkTesting::AddArguments(int argc, const char** argv)
{
  for (int i = 0; i < argc; ++i)
  {
    this->Args.push_back(argv[i]);
  }
}

//-----------------------------------------------------------------------------
void svtkTesting::AddArguments(int argc, char** argv)
{
  for (int i = 0; i < argc; ++i)
  {
    this->Args.push_back(argv[i]);
  }
}

//-----------------------------------------------------------------------------
char* svtkTesting::GetArgument(const char* argName)
{
  string argValue = svtkTestingGetArgOrEnvOrDefault(argName, this->Args, "", "");

  char* cArgValue = new char[argValue.size() + 1];
  strcpy(cArgValue, argValue.c_str());

  return cArgValue;
}
//-----------------------------------------------------------------------------
void svtkTesting::CleanArguments()
{
  this->Args.erase(this->Args.begin(), this->Args.end());
}
//-----------------------------------------------------------------------------
const char* svtkTesting::GetDataRoot()
{
#ifdef SVTK_DATA_ROOT
  string dr = svtkTestingGetArgOrEnvOrDefault("-D", this->Args, "SVTK_DATA_ROOT", SVTK_DATA_ROOT);
#else
  string dr =
    svtkTestingGetArgOrEnvOrDefault("-D", this->Args, "SVTK_DATA_ROOT", "../../../../SVTKData");
#endif
  this->SetDataRoot(svtksys::SystemTools::CollapseFullPath(dr).c_str());

  return this->DataRoot;
}
//-----------------------------------------------------------------------------
const char* svtkTesting::GetTempDirectory()
{
  string td =
    svtkTestingGetArgOrEnvOrDefault("-T", this->Args, "SVTK_TEMP_DIR", "../../../Testing/Temporary");
  this->SetTempDirectory(svtksys::SystemTools::CollapseFullPath(td).c_str());

  return this->TempDirectory;
}
//-----------------------------------------------------------------------------
const char* svtkTesting::GetValidImageFileName()
{
  this->SetValidImageFileName(nullptr);
  if (!this->IsValidImageSpecified())
  {
    return this->ValidImageFileName;
  }

  string baseline =
    svtkTestingGetArgOrEnvOrDefault("-B", this->Args, "SVTK_BASELINE_ROOT", this->GetDataRoot());

  for (size_t i = 0; i < (this->Args.size() - 1); ++i)
  {
    if (this->Args[i] == "-V")
    {
      const char* ch = this->Args[i + 1].c_str();
      if (ch[0] == '/'
#ifdef _WIN32
        || (ch[0] >= 'a' && ch[0] <= 'z' && ch[1] == ':') ||
        (ch[0] >= 'A' && ch[0] <= 'Z' && ch[1] == ':')
#endif
      )
      {
        baseline = this->Args[i + 1];
      }
      else
      {
        baseline += "/";
        baseline += this->Args[i + 1];
      }
      break;
    }
  }

  this->SetValidImageFileName(baseline.c_str());

  return this->ValidImageFileName;
}
//-----------------------------------------------------------------------------
int svtkTesting::IsInteractiveModeSpecified()
{
  for (size_t i = 0; i < this->Args.size(); ++i)
  {
    if (this->Args[i] == "-I")
    {
      return 1;
    }
  }
  return 0;
}
//-----------------------------------------------------------------------------
int svtkTesting::IsFlagSpecified(const char* flag)
{
  for (size_t i = 0; i < this->Args.size(); ++i)
  {
    if (this->Args[i] == flag)
    {
      return 1;
    }
  }
  return 0;
}
//-----------------------------------------------------------------------------
int svtkTesting::IsValidImageSpecified()
{
  for (size_t i = 1; i < this->Args.size(); ++i)
  {
    if (this->Args[i - 1] == "-V")
    {
      return 1;
    }
  }
  return 0;
}
//-----------------------------------------------------------------------------
char* svtkTesting::IncrementFileName(const char* fname, int count)
{
  char counts[256];
  snprintf(counts, sizeof(counts), "%d", count);

  int orgLen = static_cast<int>(strlen(fname));
  if (orgLen < 5)
  {
    return nullptr;
  }
  int extLen = static_cast<int>(strlen(counts));
  char* newFileName = new char[orgLen + extLen + 2];
  strcpy(newFileName, fname);

  newFileName[orgLen - 4] = '_';
  int i, marker;
  for (marker = orgLen - 3, i = 0; marker < orgLen - 3 + extLen; marker++, i++)
  {
    newFileName[marker] = counts[i];
  }
  strcpy(newFileName + marker, ".png");

  return newFileName;
}
//-----------------------------------------------------------------------------
int svtkTesting::LookForFile(const char* newFileName)
{
  if (!newFileName)
  {
    return 0;
  }
  svtksys::SystemTools::Stat_t fs;
  if (svtksys::SystemTools::Stat(newFileName, &fs) != 0)
  {
    return 0;
  }
  else
  {
    return 1;
  }
}

//-----------------------------------------------------------------------------
void svtkTesting::SetFrontBuffer(svtkTypeBool frontBuffer)
{
  svtkWarningMacro("SetFrontBuffer method is deprecated and has no effet anymore.");
  this->FrontBuffer = frontBuffer;
}

//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(svtkAlgorithm* imageSource, double thresh)
{
  int result = this->RegressionTest(imageSource, thresh, cout);

  cout << "<DartMeasurement name=\"WallTime\" type=\"numeric/double\">";
  cout << svtkTimerLog::GetUniversalTime() - this->StartWallTime;
  cout << "</DartMeasurement>\n";
  cout << "<DartMeasurement name=\"CPUTime\" type=\"numeric/double\">";
  cout << svtkTimerLog::GetCPUTime() - this->StartCPUTime;
  cout << "</DartMeasurement>\n";

  return result;
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTestAndCaptureOutput(double thresh, ostream& os)
{
  int result = this->RegressionTest(thresh, os);

  os << "<DartMeasurement name=\"WallTime\" type=\"numeric/double\">";
  os << svtkTimerLog::GetUniversalTime() - this->StartWallTime;
  os << "</DartMeasurement>\n";
  os << "<DartMeasurement name=\"CPUTime\" type=\"numeric/double\">";
  os << svtkTimerLog::GetCPUTime() - this->StartCPUTime;
  os << "</DartMeasurement>\n";

  return result;
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(double thresh)
{
  int result = this->RegressionTestAndCaptureOutput(thresh, cout);
  return result;
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(double thresh, ostream& os)
{
  svtkNew<svtkWindowToImageFilter> rtW2if;
  rtW2if->SetInput(this->RenderWindow);

  for (unsigned int i = 0; i < this->Args.size(); ++i)
  {
    if ("-FrontBuffer" == this->Args[i])
    {
      svtkWarningMacro("-FrontBuffer option is deprecated and has no effet anymore.");
      this->FrontBufferOn();
    }
    else if ("-NoRerender" == this->Args[i])
    {
      rtW2if->ShouldRerenderOff();
    }
  }

  std::ostringstream out1;
  // perform and extra render to make sure it is displayed
  int swapBuffers = this->RenderWindow->GetSwapBuffers();
  // since we're reading from back-buffer, it's essential that we turn off swapping
  // otherwise what remains in the back-buffer after the swap is undefined by OpenGL specs.
  this->RenderWindow->SwapBuffersOff();
  this->RenderWindow->Render();
  rtW2if->ReadFrontBufferOff();
  rtW2if->Update();
  this->RenderWindow->SetSwapBuffers(swapBuffers); // restore swap state.
  int res = this->RegressionTest(rtW2if, thresh, out1);
  if (res == FAILED)
  {
    std::ostringstream out2;
    // tell it to read front buffer
    rtW2if->ReadFrontBufferOn();
    rtW2if->Update();
    res = this->RegressionTest(rtW2if, thresh, out2);
    // If both tests fail, rerun the backbuffer tests to recreate the test
    // image. Otherwise an incorrect image will be uploaded to CDash.
    if (res == PASSED)
    {
      os << out2.str();
    }
    else
    {
      // we failed both back and front buffers so
      // to help us debug, write out renderwindow capabilities
      if (this->RenderWindow)
      {
        os << this->RenderWindow->ReportCapabilities();
      }
      rtW2if->ReadFrontBufferOff();
      rtW2if->Update();
      return this->RegressionTest(rtW2if, thresh, os);
    }
  }
  else
  {
    os << out1.str();
  }
  return res;
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(const string& pngFileName, double thresh)
{
  return this->RegressionTest(pngFileName, thresh, cout);
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(const string& pngFileName, double thresh, ostream& os)
{
  svtkNew<svtkPNGReader> inputReader;
  inputReader->SetFileName(pngFileName.c_str());
  inputReader->Update();

  svtkAlgorithm* src = inputReader;

  svtkSmartPointer<svtkImageExtractComponents> extract;
  // Convert rgba to rgb if needed
  if (inputReader->GetOutput() && inputReader->GetOutput()->GetNumberOfScalarComponents() == 4)
  {
    extract = svtkSmartPointer<svtkImageExtractComponents>::New();
    extract->SetInputConnection(src->GetOutputPort());
    extract->SetComponents(0, 1, 2);
    extract->Update();
    src = extract;
  }

  return this->RegressionTest(src, thresh, os);
}
//-----------------------------------------------------------------------------
int svtkTesting::RegressionTest(svtkAlgorithm* imageSource, double thresh, ostream& os)
{
  // do a get to compute the real value
  this->GetValidImageFileName();
  string tmpDir = this->GetTempDirectory();

  // construct the names for the error images
  string validName = this->ValidImageFileName;
  string::size_type slashPos = validName.rfind('/');
  if (slashPos != string::npos)
  {
    validName = validName.substr(slashPos + 1);
  }

  // We want to print the filename of the best matching image for better
  // comparisons in CDash:
  string bestImageFileName = this->ValidImageFileName;

  // check the valid image
  FILE* rtFin = svtksys::SystemTools::Fopen(this->ValidImageFileName, "r");
  if (rtFin)
  {
    fclose(rtFin);
  }
  else // there was no valid image, so write one to the temp dir
  {
    string vImage = tmpDir + "/" + validName;
    rtFin = svtksys::SystemTools::Fopen(vImage, "wb");
    if (rtFin)
    {
      fclose(rtFin);
      svtkNew<svtkPNGWriter> rtPngw;
      rtPngw->SetFileName(vImage.c_str());
      rtPngw->SetInputConnection(imageSource->GetOutputPort());
      rtPngw->Write();
      os << "<DartMeasurement name=\"ImageNotFound\" type=\"text/string\">"
         << this->ValidImageFileName << "</DartMeasurement>" << endl;
      // Write out the image upload tag for the test image.
      os << "<DartMeasurementFile name=\"TestImage\" type=\"image/png\">";
      os << vImage;
      os << "</DartMeasurementFile>";
    }
    else
    {
      svtkErrorMacro("Could not open file '" << vImage << "' for writing.");
    }
    return FAILED;
  }

  imageSource->Update();

  svtkNew<svtkPNGReader> rtPng;
  rtPng->SetFileName(this->ValidImageFileName);
  rtPng->Update();

  svtkNew<svtkImageExtractComponents> rtExtract;
  rtExtract->SetInputConnection(rtPng->GetOutputPort());
  rtExtract->SetComponents(0, 1, 2);
  rtExtract->Update();

  svtkNew<svtkImageDifference> rtId;

  svtkNew<svtkImageClip> ic1;
  ic1->SetClipData(1);
  ic1->SetInputConnection(imageSource->GetOutputPort());

  svtkNew<svtkImageClip> ic2;
  ic2->SetClipData(1);
  ic2->SetInputConnection(rtExtract->GetOutputPort());

  int* wExt1 = ic1->GetInputInformation()->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT());
  int* wExt2 = ic2->GetInputInformation()->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT());
  ic1->SetOutputWholeExtent(wExt1[0] + this->BorderOffset, wExt1[1] - this->BorderOffset,
    wExt1[2] + this->BorderOffset, wExt1[3] - this->BorderOffset, wExt1[4], wExt1[5]);

  ic2->SetOutputWholeExtent(wExt2[0] + this->BorderOffset, wExt2[1] - this->BorderOffset,
    wExt2[2] + this->BorderOffset, wExt2[3] - this->BorderOffset, wExt2[4], wExt2[5]);

  int ext1[6], ext2[6];
  rtId->SetInputConnection(ic1->GetOutputPort());
  ic1->Update();
  ic1->GetOutput()->GetExtent(ext1);
  rtId->SetImageConnection(ic2->GetOutputPort());
  ic2->Update();
  ic2->GetOutput()->GetExtent(ext2);

  double minError = SVTK_DOUBLE_MAX;

  if ((ext2[1] - ext2[0]) == (ext1[1] - ext1[0]) && (ext2[3] - ext2[2]) == (ext1[3] - ext1[2]) &&
    (ext2[5] - ext2[4]) == (ext1[5] - ext1[4]))
  {
    // Cannot compute difference unless image sizes are the same
    rtId->Update();
    minError = rtId->GetThresholdedError();
  }

  this->ImageDifference = minError;
  int passed = 0;
  if (minError <= thresh)
  {
    // Make sure there was actually a difference image before
    // accepting the error measure.
    svtkImageData* output = rtId->GetOutput();
    if (output)
    {
      int dims[3];
      output->GetDimensions(dims);
      if (dims[0] * dims[1] * dims[2] > 0)
      {
        passed = 1;
      }
      else
      {
        svtkErrorMacro("ImageDifference produced output with no data.");
      }
    }
    else
    {
      svtkErrorMacro("ImageDifference did not produce output.");
    }
  }

  // If the test failed with the first image (foo.png) check if there are
  // images of the form foo_N.png (where N=1,2,3...) and compare against
  // them.
  double error;
  int count = 1, errIndex = -1;
  char* newFileName;
  while (!passed)
  {
    newFileName = IncrementFileName(this->ValidImageFileName, count);
    if (!LookForFile(newFileName))
    {
      delete[] newFileName;
      break;
    }

    rtPng->SetFileName(newFileName);

    // Need to reset the output whole extent cause we may have baselines
    // of differing sizes. (Yes, we have such cases !)
    ic2->ResetOutputWholeExtent();
    ic2->SetOutputWholeExtent(wExt2[0] + this->BorderOffset, wExt2[1] - this->BorderOffset,
      wExt2[2] + this->BorderOffset, wExt2[3] - this->BorderOffset, wExt2[4], wExt2[5]);
    ic2->UpdateWholeExtent();

    rtId->GetImage()->GetExtent(ext2);
    if ((ext2[1] - ext2[0]) == (ext1[1] - ext1[0]) && (ext2[3] - ext2[2]) == (ext1[3] - ext1[2]) &&
      (ext2[5] - ext2[4]) == (ext1[5] - ext1[4]))
    {
      // Cannot compute difference unless image sizes are the same
      rtId->Update();
      error = rtId->GetThresholdedError();
    }
    else
    {
      error = SVTK_DOUBLE_MAX;
    }

    if (error <= thresh)
    {
      // Make sure there was actually a difference image before
      // accepting the error measure.
      svtkImageData* output = rtId->GetOutput();
      if (output)
      {
        int dims[3];
        output->GetDimensions(dims);
        if (dims[0] * dims[1] * dims[2] > 0)
        {
          minError = error;
          passed = 1;
        }
      }
    }
    else
    {
      if (error < minError)
      {
        errIndex = count;
        minError = error;
        bestImageFileName = newFileName;
      }
    }
    ++count;
    delete[] newFileName;
  }

  this->ImageDifference = minError;

  // output some information
  os << "<DartMeasurement name=\"ImageError\" type=\"numeric/double\">";
  os << minError;
  os << "</DartMeasurement>";
  if (errIndex <= 0)
  {
    os << "<DartMeasurement name=\"BaselineImage\" type=\"text/string\">Standard</DartMeasurement>";
  }
  else
  {
    os << "<DartMeasurement name=\"BaselineImage\" type=\"numeric/integer\">";
    os << errIndex;
    os << "</DartMeasurement>";
  }

  if (passed)
  {
    return PASSED;
  }

  // write out the image that was generated
  string testImageFileName = tmpDir + "/" + validName;
  FILE* testImageFile = svtksys::SystemTools::Fopen(testImageFileName, "wb");
  if (testImageFile)
  {
    fclose(testImageFile);
    svtkNew<svtkPNGWriter> rtPngw;
    rtPngw->SetFileName(testImageFileName.c_str());
    rtPngw->SetInputConnection(imageSource->GetOutputPort());
    rtPngw->Write();

    // Write out the image upload tag for the test image.
    os << "<DartMeasurementFile name=\"TestImage\" type=\"image/png\">";
    os << testImageFileName;
    os << "</DartMeasurementFile>\n";
  }
  else
  {
    svtkErrorMacro("Could not open file '" << testImageFileName
                                          << "' for "
                                             "writing.");
  }

  os << "Failed Image Test ( " << validName << " ) : " << minError << endl;
  if (errIndex >= 0)
  {
    newFileName = IncrementFileName(this->ValidImageFileName, errIndex);
    rtPng->SetFileName(newFileName);
    delete[] newFileName;
  }
  else
  {
    rtPng->SetFileName(this->ValidImageFileName);
  }

  rtPng->Update();
  rtId->GetImage()->GetExtent(ext2);

  // If no image differences produced an image, do not write a
  // difference image.
  bool hasDiff = minError > 0;
  if (!hasDiff)
  {
    os << "Image differencing failed to produce an image." << endl;
  }
  if (!((ext2[1] - ext2[0]) == (ext1[1] - ext1[0]) && (ext2[3] - ext2[2]) == (ext1[3] - ext1[2]) &&
        (ext2[5] - ext2[4]) == (ext1[5] - ext1[4])))
  {
    os << "Image differencing failed to produce an image because images are "
          "different size:"
       << endl;
    os << "Valid image: " << (ext2[1] - ext2[0] + 1) << ", " << (ext2[3] - ext2[2] + 1) << ", "
       << (ext2[5] - ext2[4] + 1) << endl;
    os << "Test image: " << (ext1[1] - ext1[0] + 1) << ", " << (ext1[3] - ext1[2] + 1) << ", "
       << (ext1[5] - ext1[4] + 1) << endl;
    return FAILED;
  }

  rtId->Update();

  // test the directory for writing
  if (hasDiff)
  {
    string diffFilename = tmpDir + "/" + validName;
    string::size_type dotPos = diffFilename.rfind('.');
    if (dotPos != string::npos)
    {
      diffFilename = diffFilename.substr(0, dotPos);
    }
    diffFilename += ".diff.png";
    FILE* rtDout = svtksys::SystemTools::Fopen(diffFilename, "wb");
    if (rtDout)
    {
      fclose(rtDout);

      // write out the difference image gamma adjusted for the dashboard
      svtkNew<svtkImageShiftScale> rtGamma;
      rtGamma->SetInputConnection(rtId->GetOutputPort());
      rtGamma->SetShift(0);
      rtGamma->SetScale(10);
      rtGamma->ClampOverflowOn();

      svtkNew<svtkPNGWriter> rtPngw;
      rtPngw->SetFileName(diffFilename.c_str());
      rtPngw->SetInputConnection(rtGamma->GetOutputPort());
      rtPngw->Write();

      os << "<DartMeasurementFile name=\"DifferenceImage\" type=\"image/png\">";
      os << diffFilename;
      os << "</DartMeasurementFile>";
    }
    else
    {
      svtkErrorMacro("Could not open file '" << diffFilename << "' for writing.");
    }
  }

  os << "<DartMeasurementFile name=\"ValidImage\" type=\"image/png\">";
  os << bestImageFileName;
  os << "</DartMeasurementFile>";

  return FAILED;
}
//-----------------------------------------------------------------------------
int svtkTesting::Test(int argc, char* argv[], svtkRenderWindow* rw, double thresh)
{
  svtkNew<svtkTesting> testing;
  for (int i = 0; i < argc; ++i)
  {
    testing->AddArgument(argv[i]);
  }

  if (testing->IsInteractiveModeSpecified())
  {
    return DO_INTERACTOR;
  }

  if (testing->IsValidImageSpecified())
  {
    testing->SetRenderWindow(rw);

    return testing->RegressionTestAndCaptureOutput(thresh, cout);
  }
  return NOT_RUN;
}
//-----------------------------------------------------------------------------
int svtkTesting::CompareAverageOfL2Norm(svtkDataArray* daA, svtkDataArray* daB, double tol)
{
  int typeA = daA->GetDataType();
  int typeB = daB->GetDataType();
  if (typeA != typeB)
  {
    svtkWarningMacro("Incompatible data types: " << typeA << "," << typeB << ".");
    return 0;
  }
  //
  svtkIdType nTupsA = daA->GetNumberOfTuples();
  svtkIdType nTupsB = daB->GetNumberOfTuples();
  int nCompsA = daA->GetNumberOfComponents();
  int nCompsB = daB->GetNumberOfComponents();
  //
  if ((nTupsA != nTupsB) || (nCompsA != nCompsB))
  {
    svtkWarningMacro("Arrays: " << daA->GetName() << " (nC=" << nCompsA << " nT= " << nTupsA << ")"
                               << " and " << daB->GetName() << " (nC=" << nCompsB
                               << " nT= " << nTupsB << ")"
                               << " do not have the same structure.");
    return 0;
  }

  double L2 = 0.0;
  svtkIdType N = 0;
  switch (typeA)
  {
    case SVTK_DOUBLE:
    {
      svtkDoubleArray* A = svtkArrayDownCast<svtkDoubleArray>(daA);
      double* pA = A->GetPointer(0);
      svtkDoubleArray* B = svtkArrayDownCast<svtkDoubleArray>(daB);
      double* pB = B->GetPointer(0);
      N = AccumulateScaledL2Norm(pA, pB, nTupsA, nCompsA, L2);
    }
    break;
    case SVTK_FLOAT:
    {
      svtkFloatArray* A = svtkArrayDownCast<svtkFloatArray>(daA);
      float* pA = A->GetPointer(0);
      svtkFloatArray* B = svtkArrayDownCast<svtkFloatArray>(daB);
      float* pB = B->GetPointer(0);
      N = AccumulateScaledL2Norm(pA, pB, nTupsA, nCompsA, L2);
    }
    break;
    default:
      if (this->Verbose)
      {
        cout << "Skipping:" << daA->GetName() << endl;
      }
      return true;
  }
  //
  if (N <= 0)
  {
    return 0;
  }
  //
  if (this->Verbose)
  {
    cout << "Sum(L2)/N of " << daA->GetName() << " < " << tol << "? = " << L2 << "/" << N << "."
         << endl;
  }
  //
  double avgL2 = L2 / static_cast<double>(N);
  if (avgL2 > tol)
  {
    return 0;
  }

  // Test passed
  return 1;
}
//-----------------------------------------------------------------------------
int svtkTesting::CompareAverageOfL2Norm(svtkDataSet* dsA, svtkDataSet* dsB, double tol)
{
  svtkDataArray* daA = nullptr;
  svtkDataArray* daB = nullptr;
  int status = 0;

  // Compare points if the dataset derives from
  // svtkPointSet.
  svtkPointSet* ptSetA = svtkPointSet::SafeDownCast(dsA);
  svtkPointSet* ptSetB = svtkPointSet::SafeDownCast(dsB);
  if (ptSetA != nullptr && ptSetB != nullptr)
  {
    if (this->Verbose)
    {
      cout << "Comparing points:" << endl;
    }
    daA = ptSetA->GetPoints()->GetData();
    daB = ptSetB->GetPoints()->GetData();
    //
    status = CompareAverageOfL2Norm(daA, daB, tol);
    if (status == 0)
    {
      return 0;
    }
  }

  // Compare point data arrays.
  if (this->Verbose)
  {
    cout << "Comparing data arrays:" << endl;
  }
  int nDaA = dsA->GetPointData()->GetNumberOfArrays();
  int nDaB = dsB->GetPointData()->GetNumberOfArrays();
  if (nDaA != nDaB)
  {
    svtkWarningMacro("Point data, " << dsA << " and " << dsB << " differ in number of arrays"
                                   << " and cannot be compared.");
    return 0;
  }
  //
  for (int arrayId = 0; arrayId < nDaA; ++arrayId)
  {
    daA = dsA->GetPointData()->GetArray(arrayId);
    daB = dsB->GetPointData()->GetArray(arrayId);
    //
    status = CompareAverageOfL2Norm(daA, daB, tol);
    if (status == 0)
    {
      return 0;
    }
  }
  // All tests passed.
  return 1;
}

//-----------------------------------------------------------------------------
int svtkTesting::InteractorEventLoop(
  int argc, char* argv[], svtkRenderWindowInteractor* iren, const char* playbackStream)
{
  bool disableReplay = false, record = false, playbackFile = false;
  std::string playbackFileName;
  for (int i = 0; i < argc; i++)
  {
    disableReplay |= (strcmp("--DisableReplay", argv[i]) == 0);
    record |= (strcmp("--Record", argv[i]) == 0);
    playbackFile |= (strcmp("--PlaybackFile", argv[i]) == 0);
    if (playbackFile && playbackFileName.empty())
    {
      if (i + 1 < argc)
      {
        playbackFileName = std::string(argv[i + 1]);
        ++i;
      }
    }
  }

  svtkNew<svtkInteractorEventRecorder> recorder;
  recorder->SetInteractor(iren);

  if (!disableReplay)
  {

    if (record)
    {
      recorder->SetFileName("svtkInteractorEventRecorder.log");
      recorder->On();
      recorder->Record();
    }
    else
    {
      if (playbackStream)
      {
        recorder->ReadFromInputStringOn();
        recorder->SetInputString(playbackStream);
        recorder->Play();

        // Without this, the "-I" option if specified will fail
        recorder->Off();
      }
      else if (playbackFile)
      {
        recorder->SetFileName(playbackFileName.c_str());
        recorder->Play();

        // Without this, the "-I" option if specified will fail
        recorder->Off();
      }
    }
  }

  // iren will be either the object factory instantiation (svtkTestingInteractor)
  // or svtkRenderWindowInteractor depending on whether or not "-I" is specified.
  iren->Start();

  recorder->Off();

  return EXIT_SUCCESS;
}

//-----------------------------------------------------------------------------
void svtkTesting::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "RenderWindow: " << this->RenderWindow << endl;
  os << indent
     << "ValidImageFileName: " << (this->ValidImageFileName ? this->ValidImageFileName : "(none)")
     << endl;
  os << indent << "FrontBuffer: " << (this->FrontBuffer ? "On" : "Off") << endl;
  os << indent << "ImageDifference: " << this->ImageDifference << endl;
  os << indent << "DataRoot: " << this->GetDataRoot() << endl;
  os << indent << "Temp Directory: " << this->GetTempDirectory() << endl;
  os << indent << "BorderOffset: " << this->GetBorderOffset() << endl;
  os << indent << "Verbose: " << this->GetVerbose() << endl;
}