import os, sys

import svtk
from svtk.test import Testing

if sys.hexversion < 0x03000000:
    # for Python2
    import Tkinter as tkinter
else:
    # for Python3
    import tkinter

from svtk.tk.svtkTkRenderWidget import svtkTkRenderWidget


class TestTkRenderWidget(Testing.svtkTest):

    # Stick your SVTK pipeline here if you want to create the pipeline
    # only once.  If you put it in the constructor or in the function
    # the pipeline will be created afresh for each and every test.

    # create a dummy Tkinter root window.
    root = tkinter.Tk()

    # create a rendering window and renderer
    ren = svtk.svtkRenderer()
    tkrw = svtkTkRenderWidget(root, width=300, height=300)
    tkrw.pack()
    tkrw.GetRenderWindow().AddRenderer(ren)

    # create an actor and give it cone geometry
    cs = svtk.svtkConeSource()
    cs.SetResolution(8)
    map = svtk.svtkPolyDataMapper()
    map.SetInputConnection(cs.GetOutputPort())
    act = svtk.svtkActor()
    act.SetMapper(map)

    # assign our actor to the renderer
    ren.AddActor(act)

    def testsvtkTkRenderWidget(self):
        "Test if svtkTkRenderWidget works."
        self.tkrw.Render()
        self.root.update()
        img_file = "TestTkRenderWidget.png"
        Testing.compareImage(self.tkrw.GetRenderWindow(),
                             Testing.getAbsImagePath(img_file))
        Testing.interact()
        self.root.destroy()

    # Dummy tests to demonstrate how the blackbox tests can be done.
    def testParse(self):
        "Test if svtkActor is parseable"
        self._testParse(self.act)

    def testGetSet(self):
        "Testing Get/Set methods"
        self._testGetSet(self.act, excluded_methods="AllocatedRenderTime")

    def testBoolean(self):
        "Testing Boolean methods"
        self._testBoolean(self.act)


if __name__ == "__main__":
    cases = [(TestTkRenderWidget, 'test')]
    del TestTkRenderWidget
    Testing.main(cases)
