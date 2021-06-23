#!/usr/bin/env python
import svtk
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

# demonstrate labeling of contour with scalar value
# Create the RenderWindow, Renderer and both Actors
#
ren1 = svtk.svtkRenderer()
renWin = svtk.svtkRenderWindow()
renWin.SetMultiSamples(0)
renWin.AddRenderer(ren1)
iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)

# Read a slice and contour it
v16 = svtk.svtkVolume16Reader()
v16.SetDataDimensions(64, 64)
v16.GetOutput().SetOrigin(0.0, 0.0, 0.0)
v16.SetDataByteOrderToLittleEndian()
v16.SetFilePrefix(SVTK_DATA_ROOT + "/Data/headsq/quarter")
v16.SetImageRange(45, 45)
v16.SetDataSpacing(3.2, 3.2, 1.5)

iso = svtk.svtkContourFilter()
iso.SetInputConnection(v16.GetOutputPort())
iso.GenerateValues(6, 500, 1150)
iso.Update()

numPts = iso.GetOutput().GetNumberOfPoints()

isoMapper = svtk.svtkPolyDataMapper()
isoMapper.SetInputConnection(iso.GetOutputPort())
isoMapper.ScalarVisibilityOn()
isoMapper.SetScalarRange(iso.GetOutput().GetScalarRange())
isoActor = svtk.svtkActor()
isoActor.SetMapper(isoMapper)

# Subsample the points and label them
mask = svtk.svtkMaskPoints()
mask.SetInputConnection(iso.GetOutputPort())
mask.SetOnRatio(numPts // 50)
mask.SetMaximumNumberOfPoints(50)
mask.RandomModeOn()

# Create labels for points - only show visible points
visPts = svtk.svtkSelectVisiblePoints()
visPts.SetInputConnection(mask.GetOutputPort())
visPts.SetRenderer(ren1)

ldm = svtk.svtkLabeledDataMapper()
ldm.SetInputConnection(mask.GetOutputPort())
#    ldm.SetLabelFormat("%g")
ldm.SetLabelModeToLabelScalars()

tprop = ldm.GetLabelTextProperty()
tprop.SetFontFamilyToArial()
tprop.SetFontSize(10)
tprop.SetColor(1, 0, 0)

contourLabels = svtk.svtkActor2D()
contourLabels.SetMapper(ldm)

# Add the actors to the renderer, set the background and size
#
ren1.AddActor2D(isoActor)
ren1.AddActor2D(contourLabels)
ren1.SetBackground(1, 1, 1)

renWin.SetSize(500, 500)

renWin.Render()
ren1.GetActiveCamera().Zoom(1.5)

# render the image
#
#iren.Start()