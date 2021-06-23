//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_BitmapFont_h
#define svtk_m_BitmapFont_h

#include <svtkm/rendering/svtkm_rendering_export.h>

#include <svtkm/Types.h>

#include <string>
#include <vector>

namespace svtkm
{
namespace rendering
{

class SVTKM_RENDERING_EXPORT BitmapFont
{
public:
  struct Character
  {
    std::string id;
    char c;
    int offx, offy;
    int x, y, w, h;
    int adv;
    int kern[256];
    Character() { ResetKerning(); }
    Character(const std::string& id_,
              char c_,
              int offx_,
              int offy_,
              int x_,
              int y_,
              int w_,
              int h_,
              int adv_)
      : id(id_)
      , c(c_)
      , offx(offx_)
      , offy(offy_)
      , x(x_)
      , y(y_)
      , w(w_)
      , h(h_)
      , adv(adv_)
    {
      ResetKerning();
    }
    Character(const std::string& id_, const int metrics[])
      : id(id_)
      , c((char)metrics[0])
      , offx(metrics[1])
      , offy(metrics[2])
      , x(metrics[3])
      , y(metrics[4])
      , w(metrics[5])
      , h(metrics[6])
      , adv(metrics[7])
    {
      ResetKerning();
    }
    void ResetKerning()
    {
      for (int i = 0; i < 256; i++)
      {
        kern[i] = 0;
      }
    }
  };

  std::string Name;
  std::string ImageFile;
  int Height;
  int Ascender;
  int Descender;
  int ImgW, ImgH;
  int PadL, PadR, PadT, PadB;
  int ShortMap[256];
  std::vector<Character> Chars;

  std::vector<unsigned char> RawImageFileData;

public:
  BitmapFont();

  const Character& GetChar(char c) const;

  SVTKM_CONT
  const std::vector<unsigned char>& GetRawImageData() const { return this->RawImageFileData; }

  svtkm::Float32 GetTextWidth(const std::string& text) const;

  void GetCharPolygon(char character,
                      svtkm::Float32& x,
                      svtkm::Float32& y,
                      svtkm::Float32& vl,
                      svtkm::Float32& vr,
                      svtkm::Float32& vt,
                      svtkm::Float32& vb,
                      svtkm::Float32& tl,
                      svtkm::Float32& tr,
                      svtkm::Float32& tt,
                      svtkm::Float32& tb,
                      char nextchar = 0) const;
};
}
} //namespace svtkm::rendering

#endif