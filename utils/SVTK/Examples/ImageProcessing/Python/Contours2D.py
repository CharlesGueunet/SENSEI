#!/usr/bin/env python

# This example shows how to sample a mathematical function over a
# volume. A slice from the volume is then extracted and then contoured
# to produce 2D contour lines.
#
import svtk

# Quadric definition. This is a type of implicit function. Here the
# coefficients to the equations are set.
quadric = svtk.svtkQuadric()
quadric.SetCoefficients(.5, 1, .2, 0, .1, 0, 0, .2, 0, 0)

# The svtkSampleFunction uses the quadric function and evaluates function
# value over a regular lattice (i.e., a volume).
sample = svtk.svtkSampleFunction()
sample.SetSampleDimensions(30, 30, 30)
sample.SetImplicitFunction(quadric)
sample.ComputeNormalsOff()
sample.Update()

# Here a single slice (i.e., image) is extracted from the volume. (Note: in
# actuality the VOI request causes the sample function to operate on just the
# slice.)
extract = svtk.svtkExtractVOI()
extract.SetInputConnection(sample.GetOutputPort())
extract.SetVOI(0, 29, 0, 29, 15, 15)
extract.SetSampleRate(1, 2, 3)

# The image is contoured to produce contour lines. Thirteen contour values
# ranging from (0,1.2) inclusive are produced.
contours = svtk.svtkContourFilter()
contours.SetInputConnection(extract.GetOutputPort())
contours.GenerateValues(13, 0.0, 1.2)

# The contour lines are mapped to the graphics library.
contMapper = svtk.svtkPolyDataMapper()
contMapper.SetInputConnection(contours.GetOutputPort())
contMapper.SetScalarRange(0.0, 1.2)

contActor = svtk.svtkActor()
contActor.SetMapper(contMapper)

# Create outline an outline of the sampled data.
outline = svtk.svtkOutlineFilter()
outline.SetInputConnection(sample.GetOutputPort())

outlineMapper = svtk.svtkPolyDataMapper()
outlineMapper.SetInputConnection(outline.GetOutputPort())

outlineActor = svtk.svtkActor()
outlineActor.SetMapper(outlineMapper)
outlineActor.GetProperty().SetColor(0, 0, 0)

# Create the renderer, render window, and interactor.
ren = svtk.svtkRenderer()
renWin = svtk.svtkRenderWindow()
renWin.AddRenderer(ren)
iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)

# Set the background color to white. Associate the actors with the
# renderer.
ren.SetBackground(1, 1, 1)
ren.AddActor(contActor)
ren.AddActor(outlineActor)

# Zoom in a little bit.
ren.ResetCamera()
ren.GetActiveCamera().Zoom(1.5)

# Initialize and start the event loop.
iren.Initialize()
renWin.Render()
iren.Start()