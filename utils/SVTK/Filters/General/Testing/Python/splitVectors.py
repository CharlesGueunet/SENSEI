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
pl3d = svtk.svtkMultiBlockPLOT3DReader()
pl3d.SetXYZFileName("" + str(SVTK_DATA_ROOT) + "/Data/combxyz.bin")
pl3d.SetQFileName("" + str(SVTK_DATA_ROOT) + "/Data/combq.bin")
pl3d.SetScalarFunctionNumber(100)
pl3d.SetVectorFunctionNumber(202)
pl3d.Update()
output = pl3d.GetOutput().GetBlock(0)

sf = svtk.svtkSplitField()
sf.SetInputData(output)
sf.SetInputField("VECTORS","POINT_DATA")
sf.Split(0,"vx")
sf.Split(1,"vy")
sf.Split(2,"vz")
#sf.Print()
aax = svtk.svtkAssignAttribute()
aax.SetInputConnection(sf.GetOutputPort())
aax.Assign("vx","SCALARS","POINT_DATA")
isoVx = svtk.svtkContourFilter()
isoVx.SetInputConnection(aax.GetOutputPort())
isoVx.SetValue(0,.38)
normalsVx = svtk.svtkPolyDataNormals()
normalsVx.SetInputConnection(isoVx.GetOutputPort())
normalsVx.SetFeatureAngle(45)
isoVxMapper = svtk.svtkPolyDataMapper()
isoVxMapper.SetInputConnection(normalsVx.GetOutputPort())
isoVxMapper.ScalarVisibilityOff()
isoVxActor = svtk.svtkActor()
isoVxActor.SetMapper(isoVxMapper)
isoVxActor.GetProperty().SetColor(1,0.7,0.6)

aay = svtk.svtkAssignAttribute()
aay.SetInputConnection(sf.GetOutputPort())
aay.Assign("vy","SCALARS","POINT_DATA")
isoVy = svtk.svtkContourFilter()
isoVy.SetInputConnection(aay.GetOutputPort())
isoVy.SetValue(0,.38)
normalsVy = svtk.svtkPolyDataNormals()
normalsVy.SetInputConnection(isoVy.GetOutputPort())
normalsVy.SetFeatureAngle(45)
isoVyMapper = svtk.svtkPolyDataMapper()
isoVyMapper.SetInputConnection(normalsVy.GetOutputPort())
isoVyMapper.ScalarVisibilityOff()
isoVyActor = svtk.svtkActor()
isoVyActor.SetMapper(isoVyMapper)
isoVyActor.GetProperty().SetColor(0.7,1,0.6)

aaz = svtk.svtkAssignAttribute()
aaz.SetInputConnection(sf.GetOutputPort())
aaz.Assign("vz","SCALARS","POINT_DATA")
isoVz = svtk.svtkContourFilter()
isoVz.SetInputConnection(aaz.GetOutputPort())
isoVz.SetValue(0,.38)
normalsVz = svtk.svtkPolyDataNormals()
normalsVz.SetInputConnection(isoVz.GetOutputPort())
normalsVz.SetFeatureAngle(45)
isoVzMapper = svtk.svtkPolyDataMapper()
isoVzMapper.SetInputConnection(normalsVz.GetOutputPort())
isoVzMapper.ScalarVisibilityOff()
isoVzActor = svtk.svtkActor()
isoVzActor.SetMapper(isoVzMapper)
isoVzActor.GetProperty().SetColor(0.4,0.5,1)

mf = svtk.svtkMergeFields()
mf.SetInputConnection(sf.GetOutputPort())
mf.SetOutputField("merged","POINT_DATA")
mf.SetNumberOfComponents(3)
mf.Merge(0,"vy",0)
mf.Merge(1,"vz",0)
mf.Merge(2,"vx",0)
#mf.Print()
aa = svtk.svtkAssignAttribute()
aa.SetInputConnection(mf.GetOutputPort())
aa.Assign("merged","SCALARS","POINT_DATA")
aa2 = svtk.svtkAssignAttribute()
aa2.SetInputConnection(aa.GetOutputPort())
aa2.Assign("SCALARS","VECTORS","POINT_DATA")
sl = svtk.svtkStreamTracer()
sl.SetInputConnection(aa2.GetOutputPort())
sl.SetStartPosition(2,-2,26)
sl.SetMaximumPropagation(40)
sl.SetInitialIntegrationStep(0.2)
sl.SetIntegrationDirectionToForward()
rf = svtk.svtkRibbonFilter()
rf.SetInputConnection(sl.GetOutputPort())
rf.SetWidth(1.0)
rf.SetWidthFactor(5)
slMapper = svtk.svtkPolyDataMapper()
slMapper.SetInputConnection(rf.GetOutputPort())
slActor = svtk.svtkActor()
slActor.SetMapper(slMapper)

outline = svtk.svtkStructuredGridOutlineFilter()
outline.SetInputData(output)
outlineMapper = svtk.svtkPolyDataMapper()
outlineMapper.SetInputConnection(outline.GetOutputPort())
outlineActor = svtk.svtkActor()
outlineActor.SetMapper(outlineMapper)
# Add the actors to the renderer, set the background and size
#
ren1.AddActor(isoVxActor)
isoVxActor.AddPosition(0,12,0)
ren1.AddActor(isoVyActor)
ren1.AddActor(isoVzActor)
isoVzActor.AddPosition(0,-12,0)
ren1.AddActor(slActor)
slActor.AddPosition(0,24,0)
ren1.AddActor(outlineActor)
outlineActor.AddPosition(0,24,0)
ren1.SetBackground(.8,.8,.8)
renWin.SetSize(320,320)

ren1.GetActiveCamera().SetPosition(-20.3093,20.55444,64.3922)
ren1.GetActiveCamera().SetFocalPoint(8.255,0.0499763,29.7631)
ren1.GetActiveCamera().SetViewAngle(30)
ren1.GetActiveCamera().SetViewUp(0,0,1)
ren1.GetActiveCamera().Dolly(0.4)
ren1.ResetCameraClippingRange()
# render the image
#
renWin.Render()
# prevent the tk window from showing up then start the event loop
# --- end of script --