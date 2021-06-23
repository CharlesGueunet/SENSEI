//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_cont_ArrayHandleGroupVecVariable_h
#define svtk_m_cont_ArrayHandleGroupVecVariable_h

#include <svtkm/cont/Algorithm.h>
#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/ArrayHandleCast.h>
#include <svtkm/cont/ArrayPortal.h>
#include <svtkm/cont/ErrorBadValue.h>
#include <svtkm/cont/RuntimeDeviceTracker.h>
#include <svtkm/cont/TryExecute.h>

#include <svtkm/Assert.h>
#include <svtkm/VecFromPortal.h>

#include <svtkm/exec/arg/FetchTagArrayDirectOut.h>

namespace svtkm
{
namespace exec
{

namespace internal
{

template <typename SourcePortalType, typename OffsetsPortalType>
class SVTKM_ALWAYS_EXPORT ArrayPortalGroupVecVariable
{
public:
  using ComponentType = typename std::remove_const<typename SourcePortalType::ValueType>::type;
  using ValueType = svtkm::VecFromPortal<SourcePortalType>;

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  ArrayPortalGroupVecVariable()
    : SourcePortal()
    , OffsetsPortal()
  {
  }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  ArrayPortalGroupVecVariable(const SourcePortalType& sourcePortal,
                              const OffsetsPortalType& offsetsPortal)
    : SourcePortal(sourcePortal)
    , OffsetsPortal(offsetsPortal)
  {
  }

  /// Copy constructor for any other ArrayPortalConcatenate with a portal type
  /// that can be copied to this portal type. This allows us to do any type
  /// casting that the portals do (like the non-const to const cast).
  SVTKM_SUPPRESS_EXEC_WARNINGS
  template <typename OtherSourcePortalType, typename OtherOffsetsPortalType>
  SVTKM_EXEC_CONT ArrayPortalGroupVecVariable(
    const ArrayPortalGroupVecVariable<OtherSourcePortalType, OtherOffsetsPortalType>& src)
    : SourcePortal(src.GetSourcePortal())
    , OffsetsPortal(src.GetOffsetsPortal())
  {
  }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  svtkm::Id GetNumberOfValues() const { return this->OffsetsPortal.GetNumberOfValues(); }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  ValueType Get(svtkm::Id index) const
  {
    svtkm::Id offsetIndex = this->OffsetsPortal.Get(index);
    svtkm::Id nextOffsetIndex;
    if (index + 1 < this->GetNumberOfValues())
    {
      nextOffsetIndex = this->OffsetsPortal.Get(index + 1);
    }
    else
    {
      nextOffsetIndex = this->SourcePortal.GetNumberOfValues();
    }

    return ValueType(this->SourcePortal,
                     static_cast<svtkm::IdComponent>(nextOffsetIndex - offsetIndex),
                     offsetIndex);
  }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  void Set(svtkm::Id svtkmNotUsed(index), const ValueType& svtkmNotUsed(value)) const
  {
    // The ValueType (VecFromPortal) operates on demand. Thus, if you set
    // something in the value, it has already been passed to the array. Perhaps
    // we should check to make sure that the value used matches the location
    // you are trying to set in the array, but we don't do that.
  }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  const SourcePortalType& GetSourcePortal() const { return this->SourcePortal; }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC_CONT
  const OffsetsPortalType& GetOffsetsPortal() const { return this->OffsetsPortal; }

private:
  SourcePortalType SourcePortal;
  OffsetsPortalType OffsetsPortal;
};

} // namespace internal (in svtkm::exec)

namespace arg
{

// We need to override the fetch for output fields using
// ArrayPortalGroupVecVariable because this portal does not behave like most
// ArrayPortals. Usually you ignore the Load and implement the Store. But if
// you ignore the Load, the VecFromPortal gets no portal to set values into.
// Instead, you need to implement the Load to point to the array portal. You
// can also ignore the Store because the data is already set in the array at
// that point.
template <typename ThreadIndicesType, typename SourcePortalType, typename OffsetsPortalType>
struct Fetch<svtkm::exec::arg::FetchTagArrayDirectOut,
             svtkm::exec::arg::AspectTagDefault,
             ThreadIndicesType,
             svtkm::exec::internal::ArrayPortalGroupVecVariable<SourcePortalType, OffsetsPortalType>>
{
  using ExecObjectType =
    svtkm::exec::internal::ArrayPortalGroupVecVariable<SourcePortalType, OffsetsPortalType>;
  using ValueType = typename ExecObjectType::ValueType;

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC
  ValueType Load(const ThreadIndicesType& indices, const ExecObjectType& arrayPortal) const
  {
    return arrayPortal.Get(indices.GetOutputIndex());
  }

  SVTKM_SUPPRESS_EXEC_WARNINGS
  SVTKM_EXEC
  void Store(const ThreadIndicesType&, const ExecObjectType&, const ValueType&) const
  {
    // We can actually ignore this because the VecFromPortal will already have
    // set new values in the array.
  }
};

} // namespace arg (in svtkm::exec)
}
} // namespace svtkm::exec

namespace svtkm
{
namespace cont
{

template <typename SourceStorageTag, typename OffsetsStorageTag>
struct SVTKM_ALWAYS_EXPORT StorageTagGroupVecVariable
{
};

namespace internal
{

template <typename SourcePortal, typename SourceStorageTag, typename OffsetsStorageTag>
class Storage<svtkm::VecFromPortal<SourcePortal>,
              svtkm::cont::StorageTagGroupVecVariable<SourceStorageTag, OffsetsStorageTag>>
{
  using ComponentType = typename SourcePortal::ValueType;
  using SourceArrayHandleType = svtkm::cont::ArrayHandle<ComponentType, SourceStorageTag>;
  using OffsetsArrayHandleType = svtkm::cont::ArrayHandle<svtkm::Id, OffsetsStorageTag>;

  SVTKM_STATIC_ASSERT_MSG(
    (std::is_same<SourcePortal, typename SourceArrayHandleType::PortalControl>::value),
    "Used invalid SourcePortal type with expected SourceStorageTag.");

public:
  using ValueType = svtkm::VecFromPortal<SourcePortal>;

  using PortalType = svtkm::exec::internal::ArrayPortalGroupVecVariable<
    typename SourceArrayHandleType::PortalControl,
    typename OffsetsArrayHandleType::PortalConstControl>;
  using PortalConstType = svtkm::exec::internal::ArrayPortalGroupVecVariable<
    typename SourceArrayHandleType::PortalConstControl,
    typename OffsetsArrayHandleType::PortalConstControl>;

  SVTKM_CONT
  Storage()
    : Valid(false)
  {
  }

  SVTKM_CONT
  Storage(const SourceArrayHandleType& sourceArray, const OffsetsArrayHandleType& offsetsArray)
    : SourceArray(sourceArray)
    , OffsetsArray(offsetsArray)
    , Valid(true)
  {
  }

  SVTKM_CONT
  PortalType GetPortal()
  {
    return PortalType(this->SourceArray.GetPortalControl(),
                      this->OffsetsArray.GetPortalConstControl());
  }

  SVTKM_CONT
  PortalConstType GetPortalConst() const
  {
    return PortalConstType(this->SourceArray.GetPortalConstControl(),
                           this->OffsetsArray.GetPortalConstControl());
  }

  SVTKM_CONT
  svtkm::Id GetNumberOfValues() const
  {
    SVTKM_ASSERT(this->Valid);
    return this->OffsetsArray.GetNumberOfValues();
  }

  SVTKM_CONT
  void Allocate(svtkm::Id svtkmNotUsed(numberOfValues))
  {
    SVTKM_ASSERT("Allocate not supported for ArrayhandleGroupVecVariable" && false);
  }

  SVTKM_CONT
  void Shrink(svtkm::Id numberOfValues)
  {
    SVTKM_ASSERT(this->Valid);
    this->OffsetsArray.Shrink(numberOfValues);
  }

  SVTKM_CONT
  void ReleaseResources()
  {
    if (this->Valid)
    {
      this->SourceArray.ReleaseResources();
      this->OffsetsArray.ReleaseResources();
    }
  }

  // Required for later use in ArrayTransfer class
  SVTKM_CONT
  const SourceArrayHandleType& GetSourceArray() const
  {
    SVTKM_ASSERT(this->Valid);
    return this->SourceArray;
  }

  // Required for later use in ArrayTransfer class
  SVTKM_CONT
  const OffsetsArrayHandleType& GetOffsetsArray() const
  {
    SVTKM_ASSERT(this->Valid);
    return this->OffsetsArray;
  }

private:
  SourceArrayHandleType SourceArray;
  OffsetsArrayHandleType OffsetsArray;
  bool Valid;
};

template <typename SourcePortal,
          typename SourceStorageTag,
          typename OffsetsStorageTag,
          typename Device>
class ArrayTransfer<svtkm::VecFromPortal<SourcePortal>,
                    svtkm::cont::StorageTagGroupVecVariable<SourceStorageTag, OffsetsStorageTag>,
                    Device>
{
public:
  using ComponentType = typename SourcePortal::ValueType;
  using ValueType = svtkm::VecFromPortal<SourcePortal>;

private:
  using StorageTag = svtkm::cont::StorageTagGroupVecVariable<SourceStorageTag, OffsetsStorageTag>;
  using StorageType = svtkm::cont::internal::Storage<ValueType, StorageTag>;

  using SourceArrayHandleType = svtkm::cont::ArrayHandle<ComponentType, SourceStorageTag>;
  using OffsetsArrayHandleType = svtkm::cont::ArrayHandle<svtkm::Id, OffsetsStorageTag>;

public:
  using PortalControl = typename StorageType::PortalType;
  using PortalConstControl = typename StorageType::PortalConstType;

  using PortalExecution = svtkm::exec::internal::ArrayPortalGroupVecVariable<
    typename SourceArrayHandleType::template ExecutionTypes<Device>::Portal,
    typename OffsetsArrayHandleType::template ExecutionTypes<Device>::PortalConst>;
  using PortalConstExecution = svtkm::exec::internal::ArrayPortalGroupVecVariable<
    typename SourceArrayHandleType::template ExecutionTypes<Device>::PortalConst,
    typename OffsetsArrayHandleType::template ExecutionTypes<Device>::PortalConst>;

  SVTKM_CONT
  ArrayTransfer(StorageType* storage)
    : SourceArray(storage->GetSourceArray())
    , OffsetsArray(storage->GetOffsetsArray())
  {
  }

  SVTKM_CONT
  svtkm::Id GetNumberOfValues() const { return this->OffsetsArray.GetNumberOfValues(); }

  SVTKM_CONT
  PortalConstExecution PrepareForInput(bool svtkmNotUsed(updateData))
  {
    return PortalConstExecution(this->SourceArray.PrepareForInput(Device()),
                                this->OffsetsArray.PrepareForInput(Device()));
  }

  SVTKM_CONT
  PortalExecution PrepareForInPlace(bool svtkmNotUsed(updateData))
  {
    return PortalExecution(this->SourceArray.PrepareForInPlace(Device()),
                           this->OffsetsArray.PrepareForInput(Device()));
  }

  SVTKM_CONT
  PortalExecution PrepareForOutput(svtkm::Id numberOfValues)
  {
    // Cannot reallocate an ArrayHandleGroupVecVariable
    SVTKM_ASSERT(numberOfValues == this->OffsetsArray.GetNumberOfValues());
    return PortalExecution(
      this->SourceArray.PrepareForOutput(this->SourceArray.GetNumberOfValues(), Device()),
      this->OffsetsArray.PrepareForInput(Device()));
  }

  SVTKM_CONT
  void RetrieveOutputData(StorageType* svtkmNotUsed(storage)) const
  {
    // Implementation of this method should be unnecessary. The internal
    // array handles should automatically retrieve the output data as
    // necessary.
  }

  SVTKM_CONT
  void Shrink(svtkm::Id numberOfValues) { this->OffsetsArray.Shrink(numberOfValues); }

  SVTKM_CONT
  void ReleaseResources()
  {
    this->SourceArray.ReleaseResourcesExecution();
    this->OffsetsArray.ReleaseResourcesExecution();
  }

private:
  SourceArrayHandleType SourceArray;
  OffsetsArrayHandleType OffsetsArray;
};

} // namespace internal

/// \brief Fancy array handle that groups values into vectors of different sizes.
///
/// It is sometimes the case that you need to run a worklet with an input or
/// output that has a different number of values per instance. For example, the
/// cells of a CellCetExplicit can have different numbers of points in each
/// cell. If inputting or outputting cells of this type, each instance of the
/// worklet might need a \c Vec of a different length. This fance array handle
/// takes an array of values and an array of offsets and groups the consecutive
/// values in Vec-like objects. The values are treated as tightly packed, so
/// that each Vec contains the values from one offset to the next. The last
/// value contains values from the last offset to the end of the array.
///
/// For example, if you have an array handle with the 9 values
/// 0,1,2,3,4,5,6,7,8 an offsets array handle with the 3 values 0,4,6 and give
/// them to an \c ArrayHandleGroupVecVariable, you get an array that looks like
/// it contains three values of Vec-like objects with the data [0,1,2,3],
/// [4,5], and [6,7,8].
///
/// Note that this version of \c ArrayHandle breaks some of the assumptions
/// about \c ArrayHandle a little bit. Typically, there is exactly one type for
/// every value in the array, and this value is also the same between the
/// control and execution environment. However, this class uses \c
/// VecFromPortal it implement a Vec-like class that has a variable number of
/// values, and this type can change between control and execution
/// environments.
///
/// The offsets array is often derived from a list of sizes for each of the
/// entries. You can use the convenience function \c
/// ConvertNumComponentsToOffsets to take an array of sizes (i.e. the number of
/// components for each entry) and get an array of offsets needed for \c
/// ArrayHandleGroupVecVariable.
///
template <typename SourceArrayHandleType, typename OffsetsArrayHandleType>
class ArrayHandleGroupVecVariable
  : public svtkm::cont::ArrayHandle<
      svtkm::VecFromPortal<typename SourceArrayHandleType::PortalControl>,
      svtkm::cont::StorageTagGroupVecVariable<typename SourceArrayHandleType::StorageTag,
                                             typename OffsetsArrayHandleType::StorageTag>>
{
  SVTKM_IS_ARRAY_HANDLE(SourceArrayHandleType);
  SVTKM_IS_ARRAY_HANDLE(OffsetsArrayHandleType);

  SVTKM_STATIC_ASSERT_MSG(
    (std::is_same<svtkm::Id, typename OffsetsArrayHandleType::ValueType>::value),
    "ArrayHandleGroupVecVariable's offsets array must contain svtkm::Id values.");

public:
  SVTKM_ARRAY_HANDLE_SUBCLASS(
    ArrayHandleGroupVecVariable,
    (ArrayHandleGroupVecVariable<SourceArrayHandleType, OffsetsArrayHandleType>),
    (svtkm::cont::ArrayHandle<
      svtkm::VecFromPortal<typename SourceArrayHandleType::PortalControl>,
      svtkm::cont::StorageTagGroupVecVariable<typename SourceArrayHandleType::StorageTag,
                                             typename OffsetsArrayHandleType::StorageTag>>));

  using ComponentType = typename SourceArrayHandleType::ValueType;

private:
  using StorageType = svtkm::cont::internal::Storage<ValueType, StorageTag>;

public:
  SVTKM_CONT
  ArrayHandleGroupVecVariable(const SourceArrayHandleType& sourceArray,
                              const OffsetsArrayHandleType& offsetsArray)
    : Superclass(StorageType(sourceArray, offsetsArray))
  {
  }
};

/// \c make_ArrayHandleGroupVecVariable is convenience function to generate an
/// ArrayHandleGroupVecVariable. It takes in an ArrayHandle of values and an
/// array handle of offsets and returns an array handle with consecutive
/// entries grouped in a Vec.
///
template <typename SourceArrayHandleType, typename OffsetsArrayHandleType>
SVTKM_CONT svtkm::cont::ArrayHandleGroupVecVariable<SourceArrayHandleType, OffsetsArrayHandleType>
make_ArrayHandleGroupVecVariable(const SourceArrayHandleType& sourceArray,
                                 const OffsetsArrayHandleType& offsetsArray)
{
  return svtkm::cont::ArrayHandleGroupVecVariable<SourceArrayHandleType, OffsetsArrayHandleType>(
    sourceArray, offsetsArray);
}

/// \c ConvertNumComponentsToOffsets takes an array of Vec sizes (i.e. the number of components in
/// each Vec) and returns an array of offsets to a packed array of such Vecs. The resulting array
/// can be used with \c ArrayHandleGroupVecVariable.
///
/// \param numComponentsArray the input array that specifies the number of components in each group
/// Vec.
///
/// \param offsetsArray (optional) the output \c ArrayHandle, which must have a value type of \c
/// svtkm::Id. If the output \c ArrayHandle is not given, it is returned.
///
/// \param sourceArraySize (optional) a reference to a \c svtkm::Id and is filled with the expected
/// size of the source values array.
///
/// \param device (optional) specifies the device on which to run the conversion.
///
template <typename NumComponentsArrayType, typename OffsetsStorage>
SVTKM_CONT void ConvertNumComponentsToOffsets(
  const NumComponentsArrayType& numComponentsArray,
  svtkm::cont::ArrayHandle<svtkm::Id, OffsetsStorage>& offsetsArray,
  svtkm::Id& sourceArraySize,
  svtkm::cont::DeviceAdapterId device = svtkm::cont::DeviceAdapterTagAny())
{
  SVTKM_IS_ARRAY_HANDLE(NumComponentsArrayType);

  sourceArraySize = svtkm::cont::Algorithm::ScanExclusive(
    device, svtkm::cont::make_ArrayHandleCast<svtkm::Id>(numComponentsArray), offsetsArray);
}

template <typename NumComponentsArrayType, typename OffsetsStorage>
SVTKM_CONT void ConvertNumComponentsToOffsets(
  const NumComponentsArrayType& numComponentsArray,
  svtkm::cont::ArrayHandle<svtkm::Id, OffsetsStorage>& offsetsArray,
  svtkm::cont::DeviceAdapterId device = svtkm::cont::DeviceAdapterTagAny())
{
  SVTKM_IS_ARRAY_HANDLE(NumComponentsArrayType);

  svtkm::Id dummy;
  svtkm::cont::ConvertNumComponentsToOffsets(numComponentsArray, offsetsArray, dummy, device);
}

template <typename NumComponentsArrayType>
SVTKM_CONT svtkm::cont::ArrayHandle<svtkm::Id> ConvertNumComponentsToOffsets(
  const NumComponentsArrayType& numComponentsArray,
  svtkm::Id& sourceArraySize,
  svtkm::cont::DeviceAdapterId device = svtkm::cont::DeviceAdapterTagAny())
{
  SVTKM_IS_ARRAY_HANDLE(NumComponentsArrayType);

  svtkm::cont::ArrayHandle<svtkm::Id> offsetsArray;
  svtkm::cont::ConvertNumComponentsToOffsets(
    numComponentsArray, offsetsArray, sourceArraySize, device);
  return offsetsArray;
}

template <typename NumComponentsArrayType>
SVTKM_CONT svtkm::cont::ArrayHandle<svtkm::Id> ConvertNumComponentsToOffsets(
  const NumComponentsArrayType& numComponentsArray,
  svtkm::cont::DeviceAdapterId device = svtkm::cont::DeviceAdapterTagAny())
{
  SVTKM_IS_ARRAY_HANDLE(NumComponentsArrayType);

  svtkm::Id dummy;
  return svtkm::cont::ConvertNumComponentsToOffsets(numComponentsArray, dummy, device);
}
}
} // namespace svtkm::cont

//=============================================================================
// Specializations of serialization related classes
/// @cond SERIALIZATION
namespace svtkm
{
namespace cont
{

template <typename SAH, typename OAH>
struct SerializableTypeString<svtkm::cont::ArrayHandleGroupVecVariable<SAH, OAH>>
{
  static SVTKM_CONT const std::string& Get()
  {
    static std::string name = "AH_GroupVecVariable<" + SerializableTypeString<SAH>::Get() + "," +
      SerializableTypeString<OAH>::Get() + ">";
    return name;
  }
};

template <typename SP, typename SST, typename OST>
struct SerializableTypeString<
  svtkm::cont::ArrayHandle<svtkm::VecFromPortal<SP>,
                          svtkm::cont::StorageTagGroupVecVariable<SST, OST>>>
  : SerializableTypeString<
      svtkm::cont::ArrayHandleGroupVecVariable<svtkm::cont::ArrayHandle<typename SP::ValueType, SST>,
                                              svtkm::cont::ArrayHandle<svtkm::Id, OST>>>
{
};
}
} // svtkm::cont

namespace mangled_diy_namespace
{

template <typename SAH, typename OAH>
struct Serialization<svtkm::cont::ArrayHandleGroupVecVariable<SAH, OAH>>
{
private:
  using Type = svtkm::cont::ArrayHandleGroupVecVariable<SAH, OAH>;
  using BaseType = svtkm::cont::ArrayHandle<typename Type::ValueType, typename Type::StorageTag>;

public:
  static SVTKM_CONT void save(BinaryBuffer& bb, const BaseType& obj)
  {
    svtkmdiy::save(bb, obj.GetStorage().GetSourceArray());
    svtkmdiy::save(bb, obj.GetStorage().GetOffsetsArray());
  }

  static SVTKM_CONT void load(BinaryBuffer& bb, BaseType& obj)
  {
    SAH src;
    OAH off;

    svtkmdiy::load(bb, src);
    svtkmdiy::load(bb, off);

    obj = svtkm::cont::make_ArrayHandleGroupVecVariable(src, off);
  }
};

template <typename SP, typename SST, typename OST>
struct Serialization<svtkm::cont::ArrayHandle<svtkm::VecFromPortal<SP>,
                                             svtkm::cont::StorageTagGroupVecVariable<SST, OST>>>
  : Serialization<
      svtkm::cont::ArrayHandleGroupVecVariable<svtkm::cont::ArrayHandle<typename SP::ValueType, SST>,
                                              svtkm::cont::ArrayHandle<svtkm::Id, OST>>>
{
};
} // diy
/// @endcond SERIALIZATION

#endif //svtk_m_cont_ArrayHandleGroupVecVariable_h