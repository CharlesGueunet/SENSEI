/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkFixedPointVolumeRayCastMapper.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkFixedPointVolumeRayCastMapper
 * @brief   A fixed point mapper for volumes
 *
 * This is a software ray caster for rendering volumes in svtkImageData.
 * It works with all input data types and up to four components. It performs
 * composite or MIP rendering, and can be intermixed with geometric data.
 * Space leaping is used to speed up the rendering process. In addition,
 * calculation are performed in 15 bit fixed point precision. This mapper
 * is threaded, and will interleave scan lines across processors.
 *
 * WARNING: This ray caster may not produce consistent results when
 * the number of threads exceeds 1. The class warns if the number of
 * threads > 1. The differences may be subtle. Applications should decide
 * if the trade-off in performance is worth the lack of consistency.
 *
 * Other limitations of this ray caster include that:
 *   - it does not do isosurface ray casting
 *   - it does only interpolate before classify compositing
 *   - it does only maximum scalar value MIP
 *
 * This mapper handles all data type from unsigned char through double.
 * However, some of the internal calcultions are performed in float and
 * therefore even the full float range may cause problems for this mapper
 * (both in scalar data values and in spacing between samples).
 *
 * Space leaping is performed by creating a sub-sampled volume. 4x4x4
 * cells in the original volume are represented by a min, max, and
 * combined gradient and flag value. The min max volume has three
 * unsigned shorts per 4x4x4 group of cells from the original volume -
 * one reprenting the minimum scalar index (the scalar value adjusted
 * to fit in the 15 bit range), the maximum scalar index, and a
 * third unsigned short which is both the maximum gradient opacity in
 * the neighborhood (an unsigned char) and the flag that is filled
 * in for the current lookup tables to indicate whether this region
 * can be skipped.
 *
 * @sa
 * svtkVolumeMapper
 */

#ifndef svtkFixedPointVolumeRayCastMapper_h
#define svtkFixedPointVolumeRayCastMapper_h

#include "svtkRenderingVolumeModule.h" // For export macro
#include "svtkVolumeMapper.h"

#define SVTKKW_FP_SHIFT 15
#define SVTKKW_FPMM_SHIFT 17
#define SVTKKW_FP_MASK 0x7fff
#define SVTKKW_FP_SCALE 32767.0

class svtkMatrix4x4;
class svtkMultiThreader;
class svtkPlaneCollection;
class svtkRenderer;
class svtkTimerLog;
class svtkVolume;
class svtkTransform;
class svtkRenderWindow;
class svtkColorTransferFunction;
class svtkPiecewiseFunction;
class svtkFixedPointVolumeRayCastMIPHelper;
class svtkFixedPointVolumeRayCastCompositeHelper;
class svtkFixedPointVolumeRayCastCompositeGOHelper;
class svtkFixedPointVolumeRayCastCompositeGOShadeHelper;
class svtkFixedPointVolumeRayCastCompositeShadeHelper;
class svtkVolumeRayCastSpaceLeapingImageFilter;
class svtkDirectionEncoder;
class svtkEncodedGradientShader;
class svtkFiniteDifferenceGradientEstimator;
class svtkRayCastImageDisplayHelper;
class svtkFixedPointRayCastImage;
class svtkDataArray;

// Forward declaration needed for use by friend declaration below.
SVTK_THREAD_RETURN_TYPE FixedPointVolumeRayCastMapper_CastRays(void* arg);
SVTK_THREAD_RETURN_TYPE svtkFPVRCMSwitchOnDataType(void* arg);

class SVTKRENDERINGVOLUME_EXPORT svtkFixedPointVolumeRayCastMapper : public svtkVolumeMapper
{
public:
  static svtkFixedPointVolumeRayCastMapper* New();
  svtkTypeMacro(svtkFixedPointVolumeRayCastMapper, svtkVolumeMapper);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Set/Get the distance between samples used for rendering
   * when AutoAdjustSampleDistances is off, or when this mapper
   * has more than 1 second allocated to it for rendering.
   */
  svtkSetMacro(SampleDistance, float);
  svtkGetMacro(SampleDistance, float);
  //@}

  //@{
  /**
   * Set/Get the distance between samples when interactive rendering is happening.
   * In this case, interactive is defined as this volume mapper having less than 1
   * second allocated for rendering. When AutoAdjustSampleDistance is On, and the
   * allocated render time is less than 1 second, then this InteractiveSampleDistance
   * will be used instead of the SampleDistance above.
   */
  svtkSetMacro(InteractiveSampleDistance, float);
  svtkGetMacro(InteractiveSampleDistance, float);
  //@}

  //@{
  /**
   * Sampling distance in the XY image dimensions. Default value of 1 meaning
   * 1 ray cast per pixel. If set to 0.5, 4 rays will be cast per pixel. If
   * set to 2.0, 1 ray will be cast for every 4 (2 by 2) pixels. This value
   * will be adjusted to meet a desired frame rate when AutoAdjustSampleDistances
   * is on.
   */
  svtkSetClampMacro(ImageSampleDistance, float, 0.1f, 100.0f);
  svtkGetMacro(ImageSampleDistance, float);
  //@}

  //@{
  /**
   * This is the minimum image sample distance allow when the image
   * sample distance is being automatically adjusted.
   */
  svtkSetClampMacro(MinimumImageSampleDistance, float, 0.1f, 100.0f);
  svtkGetMacro(MinimumImageSampleDistance, float);
  //@}

  //@{
  /**
   * This is the maximum image sample distance allow when the image
   * sample distance is being automatically adjusted.
   */
  svtkSetClampMacro(MaximumImageSampleDistance, float, 0.1f, 100.0f);
  svtkGetMacro(MaximumImageSampleDistance, float);
  //@}

  //@{
  /**
   * If AutoAdjustSampleDistances is on, the ImageSampleDistance
   * and the SampleDistance will be varied to achieve the allocated
   * render time of this prop (controlled by the desired update rate
   * and any culling in use). If this is an interactive render (more
   * than 1 frame per second) the SampleDistance will be increased,
   * otherwise it will not be altered (a binary decision, as opposed
   * to the ImageSampleDistance which will vary continuously).
   */
  svtkSetClampMacro(AutoAdjustSampleDistances, svtkTypeBool, 0, 1);
  svtkGetMacro(AutoAdjustSampleDistances, svtkTypeBool);
  svtkBooleanMacro(AutoAdjustSampleDistances, svtkTypeBool);
  //@}

  //@{
  /**
   * Automatically compute the sample distance from the data spacing.  When
   * the number of voxels is 8, the sample distance will be roughly 1/200
   * the average voxel size. The distance will grow proportionally to
   * numVoxels^(1/3) until it reaches 1/2 average voxel size when number of
   * voxels is 1E6. Note that ScalarOpacityUnitDistance is still taken into
   * account and if different than 1, will effect the sample distance.
   */
  svtkSetClampMacro(LockSampleDistanceToInputSpacing, svtkTypeBool, 0, 1);
  svtkGetMacro(LockSampleDistanceToInputSpacing, svtkTypeBool);
  svtkBooleanMacro(LockSampleDistanceToInputSpacing, svtkTypeBool);
  //@}

  //@{
  /**
   * Set/Get the number of threads to use. This by default is equal to
   * the number of available processors detected.
   * WARNING: If number of threads > 1, results may not be consistent.
   */
  void SetNumberOfThreads(int num);
  int GetNumberOfThreads();
  //@}

  //@{
  /**
   * If IntermixIntersectingGeometry is turned on, the zbuffer will be
   * captured and used to limit the traversal of the rays.
   */
  svtkSetClampMacro(IntermixIntersectingGeometry, svtkTypeBool, 0, 1);
  svtkGetMacro(IntermixIntersectingGeometry, svtkTypeBool);
  svtkBooleanMacro(IntermixIntersectingGeometry, svtkTypeBool);
  //@}

  //@{
  /**
   * What is the image sample distance required to achieve the desired time?
   * A version of this method is provided that does not require the volume
   * argument since if you are using an LODProp3D you may not know this information.
   * If you use this version you must be certain that the ray cast mapper is
   * only used for one volume (and not shared among multiple volumes)
   */
  float ComputeRequiredImageSampleDistance(float desiredTime, svtkRenderer* ren);
  float ComputeRequiredImageSampleDistance(float desiredTime, svtkRenderer* ren, svtkVolume* vol);
  //@}

  /**
   * WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
   * Initialize rendering for this volume.
   */
  void Render(svtkRenderer*, svtkVolume*) override;

  unsigned int ToFixedPointPosition(float val);
  void ToFixedPointPosition(float in[3], unsigned int out[3]);
  unsigned int ToFixedPointDirection(float dir);
  void ToFixedPointDirection(float in[3], unsigned int out[3]);
  void FixedPointIncrement(unsigned int position[3], unsigned int increment[3]);
  void GetFloatTripleFromPointer(float v[3], float* ptr);
  void GetUIntTripleFromPointer(unsigned int v[3], unsigned int* ptr);
  void ShiftVectorDown(unsigned int in[3], unsigned int out[3]);
  int CheckMinMaxVolumeFlag(unsigned int pos[3], int c);
  int CheckMIPMinMaxVolumeFlag(unsigned int pos[3], int c, unsigned short maxIdx, int flip);

  void LookupColorUC(unsigned short* colorTable, unsigned short* scalarOpacityTable,
    unsigned short index, unsigned char color[4]);
  void LookupDependentColorUC(unsigned short* colorTable, unsigned short* scalarOpacityTable,
    unsigned short index[4], int components, unsigned char color[4]);
  void LookupAndCombineIndependentColorsUC(unsigned short* colorTable[4],
    unsigned short* scalarOpacityTable[4], unsigned short index[4], float weights[4],
    int components, unsigned char color[4]);
  int CheckIfCropped(unsigned int pos[3]);

  svtkGetObjectMacro(RenderWindow, svtkRenderWindow);
  svtkGetObjectMacro(MIPHelper, svtkFixedPointVolumeRayCastMIPHelper);
  svtkGetObjectMacro(CompositeHelper, svtkFixedPointVolumeRayCastCompositeHelper);
  svtkGetObjectMacro(CompositeGOHelper, svtkFixedPointVolumeRayCastCompositeGOHelper);
  svtkGetObjectMacro(CompositeGOShadeHelper, svtkFixedPointVolumeRayCastCompositeGOShadeHelper);
  svtkGetObjectMacro(CompositeShadeHelper, svtkFixedPointVolumeRayCastCompositeShadeHelper);
  svtkGetVectorMacro(TableShift, float, 4);
  svtkGetVectorMacro(TableScale, float, 4);
  svtkGetMacro(ShadingRequired, int);
  svtkGetMacro(GradientOpacityRequired, int);

  svtkGetObjectMacro(CurrentScalars, svtkDataArray);
  svtkGetObjectMacro(PreviousScalars, svtkDataArray);

  int* GetRowBounds() { return this->RowBounds; }
  unsigned short* GetColorTable(int c) { return this->ColorTable[c]; }
  unsigned short* GetScalarOpacityTable(int c) { return this->ScalarOpacityTable[c]; }
  unsigned short* GetGradientOpacityTable(int c) { return this->GradientOpacityTable[c]; }
  svtkVolume* GetVolume() { return this->Volume; }
  unsigned short** GetGradientNormal() { return this->GradientNormal; }
  unsigned char** GetGradientMagnitude() { return this->GradientMagnitude; }
  unsigned short* GetDiffuseShadingTable(int c) { return this->DiffuseShadingTable[c]; }
  unsigned short* GetSpecularShadingTable(int c) { return this->SpecularShadingTable[c]; }

  void ComputeRayInfo(
    int x, int y, unsigned int pos[3], unsigned int dir[3], unsigned int* numSteps);

  void InitializeRayInfo(svtkVolume* vol);

  int ShouldUseNearestNeighborInterpolation(svtkVolume* vol);

  //@{
  /**
   * Set / Get the underlying image object. One will be automatically
   * created - only need to set it when using from an AMR mapper which
   * renders multiple times into the same image.
   */
  void SetRayCastImage(svtkFixedPointRayCastImage*);
  svtkGetObjectMacro(RayCastImage, svtkFixedPointRayCastImage);
  //@}

  int PerImageInitialization(svtkRenderer*, svtkVolume*, int, double*, double*, int*);
  void PerVolumeInitialization(svtkRenderer*, svtkVolume*);
  void PerSubVolumeInitialization(svtkRenderer*, svtkVolume*, int);
  void RenderSubVolume();
  void DisplayRenderedImage(svtkRenderer*, svtkVolume*);
  void AbortRender();

  void CreateCanonicalView(svtkVolume* volume, svtkImageData* image, int blend_mode,
    double viewDirection[3], double viewUp[3]);

  /**
   * Get an estimate of the rendering time for a given volume / renderer.
   * Only valid if this mapper has been used to render that volume for
   * that renderer previously. Estimate is good when the viewing parameters
   * have not changed much since that last render.
   */
  float GetEstimatedRenderTime(svtkRenderer* ren, svtkVolume* vol)
  {
    return this->RetrieveRenderTime(ren, vol);
  }
  float GetEstimatedRenderTime(svtkRenderer* ren) { return this->RetrieveRenderTime(ren); }

  //@{
  /**
   * Set/Get the window / level applied to the final color.
   * This allows brightness / contrast adjustments on the
   * final image.
   * window is the width of the window.
   * level is the center of the window.
   * Initial window value is 1.0
   * Initial level value is 0.5
   * window cannot be null but can be negative, this way
   * values will be reversed.
   * |window| can be larger than 1.0
   * level can be any real value.
   */
  svtkSetMacro(FinalColorWindow, float);
  svtkGetMacro(FinalColorWindow, float);
  svtkSetMacro(FinalColorLevel, float);
  svtkGetMacro(FinalColorLevel, float);
  //@}

  // Here to be used by the mapper to tell the helper
  // to flip the MIP comparison in order to support
  // minimum intensity blending
  svtkGetMacro(FlipMIPComparison, int);

  /**
   * WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
   * Release any graphics resources that are being consumed by this mapper.
   * The parameter window could be used to determine which graphic
   * resources to release.
   */
  void ReleaseGraphicsResources(svtkWindow*) override;

protected:
  svtkFixedPointVolumeRayCastMapper();
  ~svtkFixedPointVolumeRayCastMapper() override;

  // The helper class that displays the image
  svtkRayCastImageDisplayHelper* ImageDisplayHelper;

  // The distance between sample points along the ray
  float SampleDistance;
  float InteractiveSampleDistance;

  // The distance between rays in the image
  float ImageSampleDistance;
  float MinimumImageSampleDistance;
  float MaximumImageSampleDistance;
  svtkTypeBool AutoAdjustSampleDistances;
  svtkTypeBool LockSampleDistanceToInputSpacing;

  // Saved values used to restore
  float OldSampleDistance;
  float OldImageSampleDistance;

  // Internal method for computing matrices needed during
  // ray casting
  void ComputeMatrices(double volumeOrigin[3], double volumeSpacing[3], int volumeExtent[6],
    svtkRenderer* ren, svtkVolume* vol);

  int ComputeRowBounds(svtkRenderer* ren, int imageFlag, int rowBoundsFlag, int volumeExtent[6]);

  void CaptureZBuffer(svtkRenderer* ren);

  friend SVTK_THREAD_RETURN_TYPE FixedPointVolumeRayCastMapper_CastRays(void* arg);
  friend SVTK_THREAD_RETURN_TYPE svtkFPVRCMSwitchOnDataType(void* arg);

  svtkMultiThreader* Threader;

  svtkMatrix4x4* PerspectiveMatrix;
  svtkMatrix4x4* ViewToWorldMatrix;
  svtkMatrix4x4* ViewToVoxelsMatrix;
  svtkMatrix4x4* VoxelsToViewMatrix;
  svtkMatrix4x4* WorldToVoxelsMatrix;
  svtkMatrix4x4* VoxelsToWorldMatrix;

  svtkMatrix4x4* VolumeMatrix;

  svtkTransform* PerspectiveTransform;
  svtkTransform* VoxelsTransform;
  svtkTransform* VoxelsToViewTransform;

  // This object encapsulated the image and all related information
  svtkFixedPointRayCastImage* RayCastImage;

  int* RowBounds;
  int* OldRowBounds;

  float* RenderTimeTable;
  svtkVolume** RenderVolumeTable;
  svtkRenderer** RenderRendererTable;
  int RenderTableSize;
  int RenderTableEntries;

  void StoreRenderTime(svtkRenderer* ren, svtkVolume* vol, float t);
  float RetrieveRenderTime(svtkRenderer* ren, svtkVolume* vol);
  float RetrieveRenderTime(svtkRenderer* ren);

  svtkTypeBool IntermixIntersectingGeometry;

  float MinimumViewDistance;

  svtkColorTransferFunction* SavedRGBFunction[4];
  svtkPiecewiseFunction* SavedGrayFunction[4];
  svtkPiecewiseFunction* SavedScalarOpacityFunction[4];
  svtkPiecewiseFunction* SavedGradientOpacityFunction[4];
  int SavedColorChannels[4];
  float SavedScalarOpacityDistance[4];
  int SavedBlendMode;
  svtkImageData* SavedParametersInput;
  svtkTimeStamp SavedParametersMTime;

  svtkImageData* SavedGradientsInput;
  svtkTimeStamp SavedGradientsMTime;

  float SavedSampleDistance;

  unsigned short ColorTable[4][32768 * 3];
  unsigned short ScalarOpacityTable[4][32768];
  unsigned short GradientOpacityTable[4][256];
  int TableSize[4];
  float TableScale[4];
  float TableShift[4];

  float GradientMagnitudeScale[4];
  float GradientMagnitudeShift[4];

  unsigned short** GradientNormal;
  unsigned char** GradientMagnitude;
  unsigned short* ContiguousGradientNormal;
  unsigned char* ContiguousGradientMagnitude;

  int NumberOfGradientSlices;

  svtkDirectionEncoder* DirectionEncoder;

  svtkEncodedGradientShader* GradientShader;

  svtkFiniteDifferenceGradientEstimator* GradientEstimator;

  unsigned short DiffuseShadingTable[4][65536 * 3];
  unsigned short SpecularShadingTable[4][65536 * 3];

  int ShadingRequired;
  int GradientOpacityRequired;

  svtkDataArray* CurrentScalars;
  svtkDataArray* PreviousScalars;

  svtkRenderWindow* RenderWindow;
  svtkVolume* Volume;

  int ClipRayAgainstVolume(
    float rayStart[3], float rayEnd[3], float rayDirection[3], double bounds[6]);

  int UpdateColorTable(svtkVolume* vol);
  int UpdateGradients(svtkVolume* vol);
  int UpdateShadingTable(svtkRenderer* ren, svtkVolume* vol);
  void UpdateCroppingRegions();

  void ComputeGradients(svtkVolume* vol);

  int ClipRayAgainstClippingPlanes(
    float rayStart[3], float rayEnd[3], int numClippingPlanes, float* clippingPlanes);

  unsigned int FixedPointCroppingRegionPlanes[6];
  unsigned int CroppingRegionMask[27];

  // Get the ZBuffer value corresponding to location (x,y) where (x,y)
  // are indexing into the ImageInUse image. This must be converted to
  // the zbuffer image coordinates. Nearest neighbor value is returned.
  float GetZBufferValue(int x, int y);

  svtkFixedPointVolumeRayCastMIPHelper* MIPHelper;
  svtkFixedPointVolumeRayCastCompositeHelper* CompositeHelper;
  svtkFixedPointVolumeRayCastCompositeGOHelper* CompositeGOHelper;
  svtkFixedPointVolumeRayCastCompositeShadeHelper* CompositeShadeHelper;
  svtkFixedPointVolumeRayCastCompositeGOShadeHelper* CompositeGOShadeHelper;

  // Some variables used for ray computation
  float ViewToVoxelsArray[16];
  float WorldToVoxelsArray[16];
  float VoxelsToWorldArray[16];

  double CroppingBounds[6];

  int NumTransformedClippingPlanes;
  float* TransformedClippingPlanes;

  double SavedSpacing[3];

  // Min Max structure used to do space leaping
  unsigned short* MinMaxVolume;
  int MinMaxVolumeSize[4];
  svtkImageData* SavedMinMaxInput;
  svtkImageData* MinMaxVolumeCache;
  svtkVolumeRayCastSpaceLeapingImageFilter* SpaceLeapFilter;

  void UpdateMinMaxVolume(svtkVolume* vol);
  void FillInMaxGradientMagnitudes(int fullDim[3], int smallDim[3]);

  float FinalColorWindow;
  float FinalColorLevel;

  int FlipMIPComparison;

  void ApplyFinalColorWindowLevel();

private:
  svtkFixedPointVolumeRayCastMapper(const svtkFixedPointVolumeRayCastMapper&) = delete;
  void operator=(const svtkFixedPointVolumeRayCastMapper&) = delete;

  bool ThreadWarning;
};

inline unsigned int svtkFixedPointVolumeRayCastMapper::ToFixedPointPosition(float val)
{
  return static_cast<unsigned int>(val * SVTKKW_FP_SCALE + 0.5);
}

inline void svtkFixedPointVolumeRayCastMapper::ToFixedPointPosition(float in[3], unsigned int out[3])
{
  out[0] = static_cast<unsigned int>(in[0] * SVTKKW_FP_SCALE + 0.5);
  out[1] = static_cast<unsigned int>(in[1] * SVTKKW_FP_SCALE + 0.5);
  out[2] = static_cast<unsigned int>(in[2] * SVTKKW_FP_SCALE + 0.5);
}

inline unsigned int svtkFixedPointVolumeRayCastMapper::ToFixedPointDirection(float dir)
{
  return ((dir < 0.0) ? (static_cast<unsigned int>(-dir * SVTKKW_FP_SCALE + 0.5))
                      : (0x80000000 + static_cast<unsigned int>(dir * SVTKKW_FP_SCALE + 0.5)));
}

inline void svtkFixedPointVolumeRayCastMapper::ToFixedPointDirection(
  float in[3], unsigned int out[3])
{
  out[0] = ((in[0] < 0.0) ? (static_cast<unsigned int>(-in[0] * SVTKKW_FP_SCALE + 0.5))
                          : (0x80000000 + static_cast<unsigned int>(in[0] * SVTKKW_FP_SCALE + 0.5)));
  out[1] = ((in[1] < 0.0) ? (static_cast<unsigned int>(-in[1] * SVTKKW_FP_SCALE + 0.5))
                          : (0x80000000 + static_cast<unsigned int>(in[1] * SVTKKW_FP_SCALE + 0.5)));
  out[2] = ((in[2] < 0.0) ? (static_cast<unsigned int>(-in[2] * SVTKKW_FP_SCALE + 0.5))
                          : (0x80000000 + static_cast<unsigned int>(in[2] * SVTKKW_FP_SCALE + 0.5)));
}

inline void svtkFixedPointVolumeRayCastMapper::FixedPointIncrement(
  unsigned int position[3], unsigned int increment[3])
{
  if (increment[0] & 0x80000000)
  {
    position[0] += (increment[0] & 0x7fffffff);
  }
  else
  {
    position[0] -= increment[0];
  }
  if (increment[1] & 0x80000000)
  {
    position[1] += (increment[1] & 0x7fffffff);
  }
  else
  {
    position[1] -= increment[1];
  }
  if (increment[2] & 0x80000000)
  {
    position[2] += (increment[2] & 0x7fffffff);
  }
  else
  {
    position[2] -= increment[2];
  }
}

inline void svtkFixedPointVolumeRayCastMapper::GetFloatTripleFromPointer(float v[3], float* ptr)
{
  v[0] = *(ptr);
  v[1] = *(ptr + 1);
  v[2] = *(ptr + 2);
}

inline void svtkFixedPointVolumeRayCastMapper::GetUIntTripleFromPointer(
  unsigned int v[3], unsigned int* ptr)
{
  v[0] = *(ptr);
  v[1] = *(ptr + 1);
  v[2] = *(ptr + 2);
}

inline void svtkFixedPointVolumeRayCastMapper::ShiftVectorDown(
  unsigned int in[3], unsigned int out[3])
{
  out[0] = in[0] >> SVTKKW_FP_SHIFT;
  out[1] = in[1] >> SVTKKW_FP_SHIFT;
  out[2] = in[2] >> SVTKKW_FP_SHIFT;
}

inline int svtkFixedPointVolumeRayCastMapper::CheckMinMaxVolumeFlag(unsigned int mmpos[3], int c)
{
  svtkIdType offset = static_cast<svtkIdType>(this->MinMaxVolumeSize[3]) *
      (mmpos[2] * static_cast<svtkIdType>(this->MinMaxVolumeSize[0] * this->MinMaxVolumeSize[1]) +
        mmpos[1] * static_cast<svtkIdType>(this->MinMaxVolumeSize[0]) + mmpos[0]) +
    static_cast<svtkIdType>(c);

  return ((*(this->MinMaxVolume + 3 * offset + 2)) & 0x00ff);
}

inline int svtkFixedPointVolumeRayCastMapper::CheckMIPMinMaxVolumeFlag(
  unsigned int mmpos[3], int c, unsigned short maxIdx, int flip)
{
  svtkIdType offset = static_cast<svtkIdType>(this->MinMaxVolumeSize[3]) *
      (mmpos[2] * static_cast<svtkIdType>(this->MinMaxVolumeSize[0] * this->MinMaxVolumeSize[1]) +
        mmpos[1] * static_cast<svtkIdType>(this->MinMaxVolumeSize[0]) + mmpos[0]) +
    static_cast<svtkIdType>(c);

  if ((*(this->MinMaxVolume + 3 * offset + 2) & 0x00ff))
  {
    if (flip)
    {
      return (*(this->MinMaxVolume + 3 * offset) < maxIdx);
    }
    else
    {
      return (*(this->MinMaxVolume + 3 * offset + 1) > maxIdx);
    }
  }
  else
  {
    return 0;
  }
}

inline void svtkFixedPointVolumeRayCastMapper::LookupColorUC(unsigned short* colorTable,
  unsigned short* scalarOpacityTable, unsigned short index, unsigned char color[4])
{
  unsigned short alpha = scalarOpacityTable[index];
  color[0] = static_cast<unsigned char>(
    (colorTable[3 * index] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
  color[1] = static_cast<unsigned char>(
    (colorTable[3 * index + 1] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
  color[2] = static_cast<unsigned char>(
    (colorTable[3 * index + 2] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
  color[3] = static_cast<unsigned char>(alpha >> (SVTKKW_FP_SHIFT - 8));
}

inline void svtkFixedPointVolumeRayCastMapper::LookupDependentColorUC(unsigned short* colorTable,
  unsigned short* scalarOpacityTable, unsigned short index[4], int components,
  unsigned char color[4])
{
  unsigned short alpha;
  switch (components)
  {
    case 2:
      alpha = scalarOpacityTable[index[1]];
      color[0] = static_cast<unsigned char>(
        (colorTable[3 * index[0]] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
      color[1] = static_cast<unsigned char>(
        (colorTable[3 * index[0] + 1] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
      color[2] = static_cast<unsigned char>(
        (colorTable[3 * index[0] + 2] * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
      color[3] = static_cast<unsigned char>(alpha >> (SVTKKW_FP_SHIFT - 8));
      break;
    case 4:
      alpha = scalarOpacityTable[index[3]];
      color[0] = static_cast<unsigned char>((index[0] * alpha + 0x7fff) >> SVTKKW_FP_SHIFT);
      color[1] = static_cast<unsigned char>((index[1] * alpha + 0x7fff) >> SVTKKW_FP_SHIFT);
      color[2] = static_cast<unsigned char>((index[2] * alpha + 0x7fff) >> SVTKKW_FP_SHIFT);
      color[3] = static_cast<unsigned char>(alpha >> (SVTKKW_FP_SHIFT - 8));
      break;
  }
}

inline void svtkFixedPointVolumeRayCastMapper::LookupAndCombineIndependentColorsUC(
  unsigned short* colorTable[4], unsigned short* scalarOpacityTable[4], unsigned short index[4],
  float weights[4], int components, unsigned char color[4])
{
  unsigned int tmp[4] = { 0, 0, 0, 0 };

  for (int i = 0; i < components; i++)
  {
    unsigned short alpha =
      static_cast<unsigned short>(static_cast<float>(scalarOpacityTable[i][index[i]]) * weights[i]);
    tmp[0] += static_cast<unsigned char>(
      ((colorTable[i][3 * index[i]]) * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
    tmp[1] += static_cast<unsigned char>(
      ((colorTable[i][3 * index[i] + 1]) * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
    tmp[2] += static_cast<unsigned char>(
      ((colorTable[i][3 * index[i] + 2]) * alpha + 0x7fff) >> (2 * SVTKKW_FP_SHIFT - 8));
    tmp[3] += static_cast<unsigned char>(alpha >> (SVTKKW_FP_SHIFT - 8));
  }

  color[0] = static_cast<unsigned char>((tmp[0] > 255) ? (255) : (tmp[0]));
  color[1] = static_cast<unsigned char>((tmp[1] > 255) ? (255) : (tmp[1]));
  color[2] = static_cast<unsigned char>((tmp[2] > 255) ? (255) : (tmp[2]));
  color[3] = static_cast<unsigned char>((tmp[3] > 255) ? (255) : (tmp[3]));
}

inline int svtkFixedPointVolumeRayCastMapper::CheckIfCropped(unsigned int pos[3])
{
  int idx;

  if (pos[2] < this->FixedPointCroppingRegionPlanes[4])
  {
    idx = 0;
  }
  else if (pos[2] > this->FixedPointCroppingRegionPlanes[5])
  {
    idx = 18;
  }
  else
  {
    idx = 9;
  }

  if (pos[1] >= this->FixedPointCroppingRegionPlanes[2])
  {
    if (pos[1] > this->FixedPointCroppingRegionPlanes[3])
    {
      idx += 6;
    }
    else
    {
      idx += 3;
    }
  }

  if (pos[0] >= this->FixedPointCroppingRegionPlanes[0])
  {
    if (pos[0] > this->FixedPointCroppingRegionPlanes[1])
    {
      idx += 2;
    }
    else
    {
      idx += 1;
    }
  }

  return !(static_cast<unsigned int>(this->CroppingRegionFlags) & this->CroppingRegionMask[idx]);
}

#endif