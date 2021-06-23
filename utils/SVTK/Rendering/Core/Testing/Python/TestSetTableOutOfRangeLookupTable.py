#!/usr/bin/env python
import sys
import svtk

useBelowRangeColor = 0
if sys.argv.count("--useBelowRangeColor") > 0:
    useBelowRangeColor = 1
useAboveRangeColor = 0
if sys.argv.count("--useAboveRangeColor") > 0:
    useAboveRangeColor = 1

colors = svtk.svtkUnsignedCharArray()
colors.SetNumberOfComponents(4)
colors.SetNumberOfTuples(256)
for i in range(256):
    colors.SetTuple(i, (i, i, i, 255))

table = svtk.svtkLookupTable()
table.SetNumberOfColors(256)
table.SetRange(-0.5, 0.5)
table.SetTable(colors)
table.SetBelowRangeColor(1, 0, 0, 1)
table.SetAboveRangeColor(0, 1, 0, 1)
table.SetUseBelowRangeColor(useBelowRangeColor)
table.SetUseAboveRangeColor(useAboveRangeColor)

sphere = svtk.svtkSphereSource()
sphere.SetPhiResolution(32)
sphere.SetThetaResolution(32)
sphere.Update()

pd = sphere.GetOutput().GetPointData()
for i in range(pd.GetNumberOfArrays()):
    print(pd.GetArray(i).GetName())

mapper = svtk.svtkPolyDataMapper()
mapper.SetInputConnection(sphere.GetOutputPort())
mapper.SetScalarModeToUsePointFieldData()
mapper.SelectColorArray("Normals")
mapper.ColorByArrayComponent("Normals", 0)
mapper.SetLookupTable(table)
mapper.UseLookupTableScalarRangeOn()
mapper.InterpolateScalarsBeforeMappingOn()

actor = svtk.svtkActor()
actor.SetMapper(mapper)

renderer = svtk.svtkRenderer()
renderer.AddActor(actor)
renderer.ResetCamera()
renderer.ResetCameraClippingRange()

renWin = svtk.svtkRenderWindow()
renWin.AddRenderer(renderer)

iren = svtk.svtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)
iren.Initialize()
renWin.Render()