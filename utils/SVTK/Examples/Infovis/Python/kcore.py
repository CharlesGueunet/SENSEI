#!/usr/bin/env python

from svtk import *

# generate a random graph
source = svtkRandomGraphSource()
source.SetNumberOfVertices(15000)
source.SetAllowSelfLoops(False)
source.SetEdgeProbability(0.003)
source.SetUseEdgeProbability(True)
source.AllowParallelEdgesOff()

# compute the kcore levels for every vertex in the graph
kcore = svtkKCoreDecomposition()
kcore.AddInputConnection(source.GetOutputPort())
kcore.SetOutputArrayName("kcore")
kcore.CheckInputGraphOn()

# generate x/y coordinates for vertices based on coreness
kcoreLayout = svtkKCoreLayout()
kcoreLayout.SetGraphConnection( kcore.GetOutputPort() )
kcoreLayout.SetCartesian(True)
kcoreLayout.SetEpsilon(0.2)
kcoreLayout.SetUnitRadius(1.0)

# assign coordinats for layout purposes based on the x/y coordinates
# that are created in kcoreLayout
kcoreAssignCoords = svtkAssignCoordinates()
kcoreAssignCoords.SetInputConnection(kcoreLayout.GetOutputPort())
kcoreAssignCoords.SetXCoordArrayName("coord_x")
kcoreAssignCoords.SetYCoordArrayName("coord_y")
kcoreAssignCoords.Update()


# draw it
view = svtkGraphLayoutView()
view.AddRepresentationFromInputConnection(kcoreAssignCoords.GetOutputPort())

view.SetVertexLabelArrayName("kcore")
view.SetVertexLabelVisibility(False)
view.SetVertexColorArrayName("kcore")
view.SetColorVertices(True)

# turn off edge visibility since it isn't useful for this view
view.SetEdgeVisibility(False)

# use the coordinates assigned by kcoreAssignCoords
view.SetLayoutStrategyToPassThrough()

theme = svtkViewTheme.CreateNeonTheme()
theme.SetLineWidth(1)
theme.SetPointSize(5)
view.ApplyViewTheme(theme)
theme.FastDelete()

view.GetRenderWindow().SetSize(600, 600)
view.ResetCamera()
view.Render()

view.GetInteractor().Start()