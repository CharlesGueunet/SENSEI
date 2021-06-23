#!/usr/bin/env python
import svtk
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

# Tests generating contour bands for cells that
# have scalar values that do not monotonously climb
# from the minimum to the maximum value
pts=svtk.svtkPoints()
s=svtk.svtkDoubleArray()
coords=list()
for y in [0,1,2,3]:
    for x in [0,1,2,3]:
        pts.InsertNextPoint([x,y,0])

#       x=1,2,3,4
for v in [9,0,0,9,  # y==0
          3,4,5,3,  # y==1
          9,5,0,9,  # y==2
          0,9,9,0]: # y==3
    s.InsertNextValue(v)
polys=svtk.svtkCellArray()
for cell in [[0,1,5,4],[2,3,7,6],[8,9,13,12],[10,11,15,14]]:
    polys.InsertNextCell(4)
    for p in cell:
        polys.InsertCellPoint(p)
ds=svtk.svtkPolyData()
ds.SetPoints(pts)
ds.GetPointData().SetScalars(s)
ds.SetPolys(polys)

src=svtk.svtkTrivialProducer()
src.SetOutput(ds)

# Generate contour bands
bands = svtk.svtkBandedPolyDataContourFilter()
bands.SetScalarModeToIndex()
bands.GenerateContourEdgesOn()
bands.ClippingOff()
bands.SetInputConnection(src.GetOutputPort())

# Now add contour values, some of which are equal to point scalars
clip_values=[0,2,4,6,8,10]
for v in clip_values:
    bands.SetValue(clip_values.index(v), v)
bands.Update()

# Map the indices to clip values
out_indices=bands.GetOutput().GetCellData().GetArray("Scalars")
out_scalars=svtk.svtkDoubleArray()
out_scalars.SetName('values')
out_scalars.SetNumberOfTuples(out_indices.GetNumberOfTuples())
i = 0
while i < out_indices.GetNumberOfTuples():
    index = int(out_indices.GetValue(i))
    out_scalars.SetComponent(i,0,bands.GetValue(index))
    i+=1

# The output data with indices mapped to clip values
poly=svtk.svtkPolyData()
poly.ShallowCopy(bands.GetOutput())
poly.GetCellData().SetScalars(out_scalars)

lut = svtk.svtkLookupTable()
lut.SetRange(clip_values[0],clip_values[-1])
lut.SetRampToLinear()
lut.SetHueRange(1,1)
lut.SetSaturationRange(0,1)
lut.SetValueRange(0,1)
lut.SetNumberOfColors(len(clip_values)-1)

# Displays the contour bands
bands_mapper = svtk.svtkPolyDataMapper()
bands_mapper.SetInputDataObject(poly)
bands_mapper.ScalarVisibilityOn()
bands_mapper.SetScalarModeToUseCellData()
bands_mapper.SetScalarRange(out_scalars.GetRange())
bands_mapper.SetLookupTable(lut)
bands_mapper.UseLookupTableScalarRangeOn()
bands_actor = svtk.svtkActor()
bands_actor.SetMapper(bands_mapper)

# Displays the cell edges of the contour bands
band_cell_edges=svtk.svtkExtractEdges()
band_cell_edges.SetInputDataObject(poly)
band_cell_edges_mapper = svtk.svtkPolyDataMapper()
band_cell_edges_mapper.ScalarVisibilityOff()
band_cell_edges_mapper.SetInputConnection(band_cell_edges.GetOutputPort())
band_cell_edges_actor = svtk.svtkActor()
band_cell_edges_actor.SetMapper(band_cell_edges_mapper)
band_cell_edges_actor.GetProperty().SetColor(.4,.4,.4)

# Displays the contour edges generated by the BPDCF,
# somewhat thicker than the cell edges
band_edges_mapper = svtk.svtkPolyDataMapper()
band_edges_mapper.SetInputConnection(bands.GetOutputPort(1))
band_edges_actor = svtk.svtkActor()
band_edges_actor.SetMapper(band_edges_mapper)
band_edges_actor.GetProperty().SetColor(1,1,1)
band_edges_actor.GetProperty().SetLineWidth(1.3)

# Displays the scalars of the input points of the BPDCF
scalar_value_mapper = svtk.svtkLabeledDataMapper()
scalar_value_mapper.SetInputConnection(src.GetOutputPort())
scalar_value_mapper.SetLabelModeToLabelScalars()
scalar_value_mapper.SetLabelFormat("%1.3f")
scalar_value_actor = svtk.svtkActor2D()
scalar_value_actor.SetMapper(scalar_value_mapper)

# Set up renderer and camera
r = svtk.svtkRenderer()
r.AddViewProp(bands_actor)
r.AddViewProp(band_cell_edges_actor)
r.AddViewProp(band_edges_actor)
r.AddViewProp(scalar_value_actor)
r.SetBackground(.5,.5,.5)
cam=r.GetActiveCamera()
cam.SetPosition  (1.5,1.5,1)
cam.SetFocalPoint(1.5,1.5,0)
cam.SetViewUp(0,1,0)
cam.SetViewAngle(125)

renWin = svtk.svtkRenderWindow()
renWin.SetSize(450,450)
renWin.AddRenderer(r)
iren=svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)
iren.Start()