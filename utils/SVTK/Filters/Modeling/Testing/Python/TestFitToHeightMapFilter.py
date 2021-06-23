#!/usr/bin/env python
import svtk
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

# Create the RenderWindow, Renderer
#
ren0 = svtk.svtkRenderer()
ren0.SetViewport(0,0,0.5,1)
ren1 = svtk.svtkRenderer()
ren1.SetViewport(0.5,0,1,1)
renWin = svtk.svtkRenderWindow()
renWin.AddRenderer( ren0 )
renWin.AddRenderer( ren1 )

iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)

# Create pipeline. Load terrain data.
#
lut = svtk.svtkLookupTable()
lut.SetHueRange(0.6, 0)
lut.SetSaturationRange(1.0, 0)
lut.SetValueRange(0.5, 1.0)

# Read the data: a height field results
demReader = svtk.svtkDEMReader()
demReader.SetFileName(SVTK_DATA_ROOT + "/Data/SainteHelens.dem")
demReader.Update()

lo = demReader.GetOutput().GetScalarRange()[0]
hi = demReader.GetOutput().GetScalarRange()[1]
bds = demReader.GetOutput().GetBounds()
#print("Bounds: {0}".format(bds))

surface = svtk.svtkImageDataGeometryFilter()
surface.SetInputConnection(demReader.GetOutputPort())

tris = svtk.svtkTriangleFilter()
tris.SetInputConnection(surface.GetOutputPort())

warp = svtk.svtkWarpScalar()
warp.SetInputConnection(tris.GetOutputPort())
warp.SetScaleFactor(1)
warp.UseNormalOn()
warp.SetNormal(0, 0, 1)

# Show the terrain
demMapper = svtk.svtkPolyDataMapper()
demMapper.SetInputConnection(warp.GetOutputPort())
demMapper.SetScalarRange(lo, hi)
demMapper.SetLookupTable(lut)

demActor = svtk.svtkActor()
demActor.SetMapper(demMapper)

# Create polygon(s) to fit. z-values are arbitrary.
polygons = svtk.svtkPolyData()

pts = svtk.svtkPoints()
pts.SetNumberOfPoints(14)
pts.SetPoint(0, 560000, 5110000, 0)
pts.SetPoint(1, 560250, 5110000, 0)
pts.SetPoint(2, 560250, 5110250, 0)
pts.SetPoint(3, 560000, 5110250, 0)

pts.SetPoint(4, 560500, 5110000, 0)
pts.SetPoint(5, 560750, 5110000, 0)
pts.SetPoint(6, 560750, 5110250, 0)
pts.SetPoint(7, 560500, 5110250, 0)

pts.SetPoint(8, 559800, 5110500, 0)
pts.SetPoint(9, 560500, 5110500, 0)
pts.SetPoint(10, 560500, 5110800, 0)
pts.SetPoint(11, 560150, 5110800, 0)
pts.SetPoint(12, 560150, 5111100, 0)
pts.SetPoint(13, 559800, 5111100, 0)

polys = svtk.svtkCellArray()
polys.InsertNextCell(4)
polys.InsertCellPoint(0)
polys.InsertCellPoint(1)
polys.InsertCellPoint(2)
polys.InsertCellPoint(3)
polys.InsertNextCell(4)
polys.InsertCellPoint(4)
polys.InsertCellPoint(5)
polys.InsertCellPoint(6)
polys.InsertCellPoint(7)
polys.InsertNextCell(6)
polys.InsertCellPoint(8)
polys.InsertCellPoint(9)
polys.InsertCellPoint(10)
polys.InsertCellPoint(11)
polys.InsertCellPoint(12)
polys.InsertCellPoint(13)

polygons.SetPoints(pts)
polygons.SetPolys(polys)

# Fit polygons to surface
fit = svtk.svtkFitToHeightMapFilter()
fit.SetInputData(polygons)
fit.SetHeightMapConnection(demReader.GetOutputPort())
fit.SetFittingStrategyToPointProjection()
fit.UseHeightMapOffsetOn()
fit.Update()

mapper = svtk.svtkPolyDataMapper()
mapper.SetInputConnection(fit.GetOutputPort())
mapper.ScalarVisibilityOff()

actor = svtk.svtkActor()
actor.SetMapper(mapper)
actor.GetProperty().SetColor(1,0,0)

# Fit polygons to surface (cell strategy)
fit2 = svtk.svtkFitToHeightMapFilter()
fit2.SetInputData(polygons)
fit2.SetHeightMapConnection(demReader.GetOutputPort())
fit2.SetFittingStrategyToCellAverageHeight()
fit2.UseHeightMapOffsetOn()
fit2.Update()

mapper2 = svtk.svtkPolyDataMapper()
mapper2.SetInputConnection(fit2.GetOutputPort())
mapper2.ScalarVisibilityOff()

actor2 = svtk.svtkActor()
actor2.SetMapper(mapper2)
actor2.GetProperty().SetColor(1,0,0)

# Render it
ren0.AddActor(demActor)
ren0.AddActor(actor)
ren1.AddActor(demActor)
ren1.AddActor(actor2)

ren0.GetActiveCamera().SetPosition( 560752, 5110002, 2110)
ren0.GetActiveCamera().SetFocalPoint( 560750, 5110000, 2100)
ren0.ResetCamera()
ren0.GetActiveCamera().SetClippingRange(269.775, 34560.4)
ren0.GetActiveCamera().SetFocalPoint(562026, 5.1135e+006, -400.794)
ren0.GetActiveCamera().SetPosition(556898, 5.10151e+006, 7906.19)

ren1.SetActiveCamera(ren0.GetActiveCamera())

renWin.Render()
iren.Start()