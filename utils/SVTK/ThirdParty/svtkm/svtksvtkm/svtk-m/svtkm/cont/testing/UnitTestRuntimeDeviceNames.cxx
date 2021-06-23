//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#include <svtkm/cont/RuntimeDeviceInformation.h>

#include <svtkm/cont/cuda/DeviceAdapterCuda.h>
#include <svtkm/cont/openmp/DeviceAdapterOpenMP.h>
#include <svtkm/cont/serial/DeviceAdapterSerial.h>
#include <svtkm/cont/tbb/DeviceAdapterTBB.h>

#include <svtkm/cont/testing/Testing.h>

#include <cctype> //for tolower

namespace
{

template <typename Tag>
void TestName(const std::string& name, Tag tag, svtkm::cont::DeviceAdapterId id)
{
  svtkm::cont::RuntimeDeviceInformation info;

  SVTKM_TEST_ASSERT(id.GetName() == name, "Id::GetName() failed.");
  SVTKM_TEST_ASSERT(tag.GetName() == name, "Tag::GetName() failed.");
  SVTKM_TEST_ASSERT(svtkm::cont::make_DeviceAdapterId(id.GetValue()) == id,
                   "make_DeviceAdapterId(int8) failed");

  SVTKM_TEST_ASSERT(info.GetName(id) == name, "RDeviceInfo::GetName(Id) failed.");
  SVTKM_TEST_ASSERT(info.GetName(tag) == name, "RDeviceInfo::GetName(Tag) failed.");
  SVTKM_TEST_ASSERT(info.GetId(name) == id, "RDeviceInfo::GetId(name) failed.");

  //check going from name to device id
  auto lowerCaseFunc = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };

  auto upperCaseFunc = [](char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  };

  if (id.IsValueValid())
  { //only test make_DeviceAdapterId with valid device ids
    SVTKM_TEST_ASSERT(
      svtkm::cont::make_DeviceAdapterId(name) == id, "make_DeviceAdapterId(", name, ") failed");

    std::string casedName = name;
    std::transform(casedName.begin(), casedName.end(), casedName.begin(), lowerCaseFunc);
    SVTKM_TEST_ASSERT(
      svtkm::cont::make_DeviceAdapterId(casedName) == id, "make_DeviceAdapterId(", name, ") failed");

    std::transform(casedName.begin(), casedName.end(), casedName.begin(), upperCaseFunc);
    SVTKM_TEST_ASSERT(
      svtkm::cont::make_DeviceAdapterId(casedName) == id, "make_DeviceAdapterId(", name, ") failed");
  }
}

void TestNames()
{
  svtkm::cont::DeviceAdapterTagUndefined undefinedTag;
  svtkm::cont::DeviceAdapterTagSerial serialTag;
  svtkm::cont::DeviceAdapterTagTBB tbbTag;
  svtkm::cont::DeviceAdapterTagOpenMP openmpTag;
  svtkm::cont::DeviceAdapterTagCuda cudaTag;

  TestName("Undefined", undefinedTag, undefinedTag);
  TestName("Serial", serialTag, serialTag);
  TestName("TBB", tbbTag, tbbTag);
  TestName("OpenMP", openmpTag, openmpTag);
  TestName("Cuda", cudaTag, cudaTag);
}

} // end anon namespace

int UnitTestRuntimeDeviceNames(int argc, char* argv[])
{
  return svtkm::cont::testing::Testing::Run(TestNames, argc, argv);
}