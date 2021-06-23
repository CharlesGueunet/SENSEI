#!/usr/bin/env python
# -*- coding: utf-8 -*-

'''
=========================================================================

  Program:   Visualization Toolkit
  Module:    TestNamedColorsIntegration.py

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================
'''

import svtk
import svtk.test.Testing
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

class TestAllLogic(svtk.test.Testing.svtkTest):

    def testAllLogic(self):

        # append multiple displaced spheres into an RGB image.


        # Image pipeline

        renWin = svtk.svtkRenderWindow()

        logics = ["And", "Or", "Xor", "Nand", "Nor", "Not"]
        types = ["Float", "Double", "UnsignedInt", "UnsignedLong", "UnsignedShort", "UnsignedChar"]

        sphere1 = list()
        sphere2 = list()
        logic = list()
        mapper = list()
        actor = list()
        imager = list()

        for idx, operator in enumerate(logics):
            ScalarType = types[idx]

            sphere1.append(svtk.svtkImageEllipsoidSource())
            sphere1[idx].SetCenter(95, 100, 0)
            sphere1[idx].SetRadius(70, 70, 70)
            eval('sphere1[idx].SetOutputScalarTypeTo' + ScalarType + '()')
            sphere1[idx].Update()

            sphere2.append(svtk.svtkImageEllipsoidSource())
            sphere2[idx].SetCenter(161, 100, 0)
            sphere2[idx].SetRadius(70, 70, 70)
            eval('sphere2[idx].SetOutputScalarTypeTo' + ScalarType + '()')
            sphere2[idx].Update()

            logic.append(svtk.svtkImageLogic())
            logic[idx].SetInput1Data(sphere1[idx].GetOutput())
            if operator != "Not":
                logic[idx].SetInput2Data(sphere2[idx].GetOutput())

            logic[idx].SetOutputTrueValue(150)
            eval('logic[idx].SetOperationTo' + operator + '()')

            mapper.append(svtk.svtkImageMapper())
            mapper[idx].SetInputConnection(logic[idx].GetOutputPort())
            mapper[idx].SetColorWindow(255)
            mapper[idx].SetColorLevel(127.5)

            actor.append(svtk.svtkActor2D())
            actor[idx].SetMapper(mapper[idx])

            imager.append(svtk.svtkRenderer())
            imager[idx].AddActor2D(actor[idx])

            renWin.AddRenderer(imager[idx])



        imager[0].SetViewport(0, .5, .33, 1)
        imager[1].SetViewport(.33, .5, .66, 1)
        imager[2].SetViewport(.66, .5, 1, 1)
        imager[3].SetViewport(0, 0, .33, .5)
        imager[4].SetViewport(.33, 0, .66, .5)
        imager[5].SetViewport(.66, 0, 1, .5)

        renWin.SetSize(768, 512)

        # render and interact with data

        iRen = svtk.svtkRenderWindowInteractor()
        iRen.SetRenderWindow(renWin);
        renWin.Render()

        img_file = "TestAllLogic.png"
        svtk.test.Testing.compareImage(iRen.GetRenderWindow(), svtk.test.Testing.getAbsImagePath(img_file), threshold=25)
        svtk.test.Testing.interact()

if __name__ == "__main__":
     svtk.test.Testing.main([(TestAllLogic, 'test')])