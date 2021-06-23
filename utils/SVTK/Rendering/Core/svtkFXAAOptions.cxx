/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkFXAAOptions.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkFXAAOptions.h"

#include "svtkObjectFactory.h"

svtkStandardNewMacro(svtkFXAAOptions);

//------------------------------------------------------------------------------
void svtkFXAAOptions::PrintSelf(std::ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "RelativeContrastThreshold: " << this->RelativeContrastThreshold << "\n";
  os << indent << "HardContrastThreshold: " << this->HardContrastThreshold << "\n";
  os << indent << "SubpixelBlendLimit: " << this->SubpixelBlendLimit << "\n";
  os << indent << "SubpixelContrastThreshold: " << this->SubpixelContrastThreshold << "\n";
  os << indent << "EndpointSearchIterations: " << this->EndpointSearchIterations << "\n";
  os << indent << "UseHighQualityEndpoints: " << this->UseHighQualityEndpoints << "\n";

  os << indent << "DebugOptionValue: ";
  switch (this->DebugOptionValue)
  {
    default:
    case svtkFXAAOptions::FXAA_NO_DEBUG:
      os << "FXAA_NO_DEBUG\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_SUBPIXEL_ALIASING:
      os << "FXAA_DEBUG_SUBPIXEL_ALIASING\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_EDGE_DIRECTION:
      os << "FXAA_DEBUG_EDGE_DIRECTION\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_EDGE_NUM_STEPS:
      os << "FXAA_DEBUG_EDGE_NUM_STEPS\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_EDGE_DISTANCE:
      os << "FXAA_DEBUG_EDGE_DISTANCE\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_EDGE_SAMPLE_OFFSET:
      os << "FXAA_DEBUG_EDGE_SAMPLE_OFFSET\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_ONLY_SUBPIX_AA:
      os << "FXAA_DEBUG_ONLY_SUBPIX_AA\n";
      break;
    case svtkFXAAOptions::FXAA_DEBUG_ONLY_EDGE_AA:
      os << "FXAA_DEBUG_ONLY_EDGE_AA\n";
      break;
  }
}

//------------------------------------------------------------------------------
svtkFXAAOptions::svtkFXAAOptions()
  : RelativeContrastThreshold(1.f / 8.f)
  , HardContrastThreshold(1.f / 16.f)
  , SubpixelBlendLimit(3.f / 4.f)
  , SubpixelContrastThreshold(1.f / 4.f)
  , EndpointSearchIterations(12)
  , UseHighQualityEndpoints(true)
  , DebugOptionValue(svtkFXAAOptions::FXAA_NO_DEBUG)
{
}

//------------------------------------------------------------------------------
svtkFXAAOptions::~svtkFXAAOptions() = default;