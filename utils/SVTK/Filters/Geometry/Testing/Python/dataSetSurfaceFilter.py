#!/usr/bin/env python
import svtk
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

# create pipeline - structured grid
#
pl3d = svtk.svtkMultiBlockPLOT3DReader()
pl3d.SetXYZFileName("" + str(SVTK_DATA_ROOT) + "/Data/combxyz.bin")
pl3d.SetQFileName("" + str(SVTK_DATA_ROOT) + "/Data/combq.bin")
pl3d.SetScalarFunctionNumber(100)
pl3d.SetVectorFunctionNumber(202)
pl3d.Update()
output = pl3d.GetOutput().GetBlock(0)
gf = svtk.svtkDataSetSurfaceFilter()
gf.SetInputData(output)
gMapper = svtk.svtkPolyDataMapper()
gMapper.SetInputConnection(gf.GetOutputPort())
gMapper.SetScalarRange(output.GetScalarRange())
gActor = svtk.svtkActor()
gActor.SetMapper(gMapper)
gf2 = svtk.svtkDataSetSurfaceFilter()
gf2.SetInputData(output)
g2Mapper = svtk.svtkPolyDataMapper()
g2Mapper.SetInputConnection(gf2.GetOutputPort())
g2Mapper.SetScalarRange(output.GetScalarRange())
g2Actor = svtk.svtkActor()
g2Actor.SetMapper(g2Mapper)
g2Actor.AddPosition(0,15,0)
# create pipeline - poly data
#
gf3 = svtk.svtkDataSetSurfaceFilter()
gf3.SetInputConnection(gf.GetOutputPort())
g3Mapper = svtk.svtkPolyDataMapper()
g3Mapper.SetInputConnection(gf3.GetOutputPort())
g3Mapper.SetScalarRange(output.GetScalarRange())
g3Actor = svtk.svtkActor()
g3Actor.SetMapper(g3Mapper)
g3Actor.AddPosition(0,0,15)
gf4 = svtk.svtkDataSetSurfaceFilter()
gf4.SetInputConnection(gf2.GetOutputPort())
gf4.UseStripsOn()
g4Mapper = svtk.svtkPolyDataMapper()
g4Mapper.SetInputConnection(gf4.GetOutputPort())
g4Mapper.SetScalarRange(output.GetScalarRange())
g4Actor = svtk.svtkActor()
g4Actor.SetMapper(g4Mapper)
g4Actor.AddPosition(0,15,15)
# create pipeline - unstructured grid
#
s = svtk.svtkSphere()
s.SetCenter(output.GetCenter())
s.SetRadius(100.0)
#everything
eg = svtk.svtkExtractGeometry()
eg.SetInputData(output)
eg.SetImplicitFunction(s)
gf5 = svtk.svtkDataSetSurfaceFilter()
gf5.SetInputConnection(eg.GetOutputPort())
g5Mapper = svtk.svtkPolyDataMapper()
g5Mapper.SetInputConnection(gf5.GetOutputPort())
g5Mapper.SetScalarRange(output.GetScalarRange())
g5Actor = svtk.svtkActor()
g5Actor.SetMapper(g5Mapper)
g5Actor.AddPosition(0,0,30)
gf6 = svtk.svtkDataSetSurfaceFilter()
gf6.SetInputConnection(eg.GetOutputPort())
gf6.UseStripsOn()
g6Mapper = svtk.svtkPolyDataMapper()
g6Mapper.SetInputConnection(gf6.GetOutputPort())
g6Mapper.SetScalarRange(output.GetScalarRange())
g6Actor = svtk.svtkActor()
g6Actor.SetMapper(g6Mapper)
g6Actor.AddPosition(0,15,30)
# create pipeline - rectilinear grid
#
rgridReader = svtk.svtkRectilinearGridReader()
rgridReader.SetFileName("" + str(SVTK_DATA_ROOT) + "/Data/RectGrid2.svtk")
rgridReader.Update()
gf7 = svtk.svtkDataSetSurfaceFilter()
gf7.SetInputConnection(rgridReader.GetOutputPort())
g7Mapper = svtk.svtkPolyDataMapper()
g7Mapper.SetInputConnection(gf7.GetOutputPort())
g7Mapper.SetScalarRange(rgridReader.GetOutput().GetScalarRange())
g7Actor = svtk.svtkActor()
g7Actor.SetMapper(g7Mapper)
g7Actor.SetScale(3,3,3)
gf8 = svtk.svtkDataSetSurfaceFilter()
gf8.SetInputConnection(rgridReader.GetOutputPort())
gf8.UseStripsOn()
g8Mapper = svtk.svtkPolyDataMapper()
g8Mapper.SetInputConnection(gf8.GetOutputPort())
g8Mapper.SetScalarRange(rgridReader.GetOutput().GetScalarRange())
g8Actor = svtk.svtkActor()
g8Actor.SetMapper(g8Mapper)
g8Actor.SetScale(3,3,3)
g8Actor.AddPosition(0,15,0)
# Create the RenderWindow, Renderer and both Actors
ren1 = svtk.svtkRenderer()
renWin = svtk.svtkRenderWindow()
renWin.AddRenderer(ren1)
iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)
ren1.AddActor(gActor)
ren1.AddActor(g2Actor)
ren1.AddActor(g3Actor)
ren1.AddActor(g4Actor)
ren1.AddActor(g5Actor)
ren1.AddActor(g6Actor)
ren1.AddActor(g7Actor)
ren1.AddActor(g8Actor)
renWin.SetSize(340,550)
cam1 = ren1.GetActiveCamera()
cam1.SetClippingRange(84,174)
cam1.SetFocalPoint(5.22824,6.09412,35.9813)
cam1.SetPosition(100.052,62.875,102.818)
cam1.SetViewUp(-0.307455,-0.464269,0.830617)
iren.Initialize()
# render the image
#
# prevent the tk window from showing up then start the event loop
# --- end of script --