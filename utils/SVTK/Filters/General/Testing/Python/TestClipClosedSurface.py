#!/usr/bin/env python
import svtk
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

# Create the RenderWindow, Renderer and both Actors
#
ren1 = svtk.svtkRenderer()
renWin = svtk.svtkRenderWindow()
renWin.AddRenderer(ren1)
iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)
# create pipeline
#
v16 = svtk.svtkVolume16Reader()
v16.SetDataDimensions(64,64)
v16.GetOutput().SetOrigin(0.0,0.0,0.0)
v16.SetDataByteOrderToLittleEndian()
v16.SetFilePrefix("" + str(SVTK_DATA_ROOT) + "/Data/headsq/quarter")
v16.SetImageRange(1,93)
v16.SetDataSpacing(3.2,3.2,1.5)
v16.Update()
iso = svtk.svtkImageMarchingCubes()
iso.SetInputConnection(v16.GetOutputPort())
iso.SetValue(0,1150)
iso.SetInputMemoryLimit(1000)
topPlane = svtk.svtkPlane()
topPlane.SetNormal(0,0,1)
topPlane.SetOrigin(0,0,0.5)
botPlane = svtk.svtkPlane()
botPlane.SetNormal(0,0,-1)
botPlane.SetOrigin(0,0,137.0)
sagPlane = svtk.svtkPlane()
sagPlane.SetNormal(1,0,0)
sagPlane.SetOrigin(100.8,0,0)
capPlanes = svtk.svtkPlaneCollection()
capPlanes.AddItem(topPlane)
capPlanes.AddItem(botPlane)
capPlanes.AddItem(sagPlane)
clip = svtk.svtkClipClosedSurface()
clip.SetClippingPlanes(capPlanes)
clip.SetInputConnection(iso.GetOutputPort())
clip.SetBaseColor(0.9804,0.9216,0.8431)
clip.SetClipColor(1.0,1.0,1.0)
clip.SetActivePlaneColor(1.0,1.0,0.8)
clip.SetActivePlaneId(2)
clip.SetScalarModeToColors()
clip.GenerateOutlineOn()
clip.GenerateFacesOn()
isoMapper = svtk.svtkPolyDataMapper()
isoMapper.SetInputConnection(clip.GetOutputPort())
isoMapper.ScalarVisibilityOn()
isoActor = svtk.svtkActor()
isoActor.SetMapper(isoMapper)
outline = svtk.svtkOutlineFilter()
outline.SetInputConnection(v16.GetOutputPort())
outlineMapper = svtk.svtkPolyDataMapper()
outlineMapper.SetInputConnection(outline.GetOutputPort())
outlineActor = svtk.svtkActor()
outlineActor.SetMapper(outlineMapper)
outlineActor.VisibilityOff()
# Add the actors to the renderer, set the background and size
#
ren1.AddActor(outlineActor)
ren1.AddActor(isoActor)
ren1.SetBackground(0.2,0.3,0.4)
renWin.SetSize(200,200)
ren1.ResetCamera()
ren1.GetActiveCamera().Elevation(90)
ren1.GetActiveCamera().SetViewUp(0,0,-1)
ren1.GetActiveCamera().Azimuth(270)
ren1.ResetCameraClippingRange()
renWin.Render()
# render the image
#
# prevent the tk window from showing up then start the event loop
# --- end of script --