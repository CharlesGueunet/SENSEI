#!/usr/bin/env python

'''
This little example shows how a cursor can be created in
 image viewers, and renderers.  The standard TkImageViewerWidget and
 TkRenderWidget bindings are used.  There is a new binding:
 middle button in the image viewer sets the position of the cursor.
'''

import sys
from functools import partial
import svtk
from svtk.test import Testing
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

if sys.hexversion < 0x03000000:
    # for Python2
    import Tkinter as tkinter
    from Tkinter import Pack
else:
    # for Python3
    import tkinter
    from tkinter import Pack

#from svtk.tk.svtkTkRenderWindowInteractor import svtkTkRenderWindowInteractor
from svtk.tk.svtkTkRenderWidget import svtkTkRenderWidget
from svtk.tk.svtkTkImageViewerWidget import svtkTkImageViewerWidget

# Tkinter constants.
E = tkinter.E
W = tkinter.W
N = tkinter.N
S = tkinter.S
HORIZONTAL = tkinter.HORIZONTAL
VERTICAL = tkinter.VERTICAL
RIGHT = tkinter.RIGHT
LEFT = tkinter.LEFT
TOP = tkinter.TOP
BOTTOM = tkinter.BOTTOM
X = tkinter.X
BOTH = tkinter.BOTH
NO = tkinter.NO
YES = tkinter.YES
NORMAL = tkinter.NORMAL
DISABLED = tkinter.DISABLED
TRUE = tkinter.TRUE
FALSE = tkinter.FALSE

# Global values.
CURSOR_X = 20
CURSOR_Y = 20
CURSOR_Z = 20

IMAGE_MAG_X = 4
IMAGE_MAG_Y = 4
IMAGE_MAG_Z = 1


class Cursor3DViewer(Testing.svtkTest):
    '''
    Provide a testing framework for for cursor3D.

    Note:
        root, the top-level widget for Tk,
        tkrw, the svtkTkRenderWidget and
        tkvw, the svtkTkImageViewerWidget
        are accessible from any function in this class
        after SetUp() has run.
    '''

    def SetUp(self):
        '''
        Set up cursor3D
        '''

        def OnClosing():
            self.root.quit()

        def ViewerDown(tkvw):
            viewer = tkvw.GetImageViewer()
            ViewerSetZSlice(tkvw, viewer.GetZSlice() - 1)

        def ViewerUp(tkvw):
            viewer = tkvw.GetImageViewer()
            ViewerSetZSlice(tkvw, viewer.GetZSlice() + 1)

        def ViewerSetZSlice(tkvw, z):
            viewer = tkvw.GetImageViewer()
            viewer.SetZSlice(z)
            txt = 'slice: ' + str(z)
            sliceLabel.configure(text=txt)
            tkvw.Render()

        def SetCursorFromViewer(event):
            x = int(event.x)
            y = int(event.y)
            # We have to flip y axis because tk uses upper right origin.
            self.root.update_idletasks()
            height = int(self.tkvw.configure()['height'][4])
            y = height - y
            z = self.tkvw.GetImageViewer().GetZSlice()
            SetCursor( x / IMAGE_MAG_X, y / IMAGE_MAG_Y, z / IMAGE_MAG_Z )

        def SetCursor(x, y, z):

            CURSOR_X = x
            CURSOR_Y = y
            CURSOR_Z = z

            axes.SetOrigin(CURSOR_X,CURSOR_Y,CURSOR_Z)
            imageCursor.SetCursorPosition(
                CURSOR_X * IMAGE_MAG_X,
                CURSOR_Y * IMAGE_MAG_Y,
                CURSOR_Z * IMAGE_MAG_Z)

            self.viewer.Render()
            self.tkrw.Render()

        # Pipeline stuff.
        reader = svtk.svtkSLCReader()
        reader.SetFileName(SVTK_DATA_ROOT + "/Data/neghip.slc")
        # Cursor stuff

        magnify = svtk.svtkImageMagnify()
        magnify.SetInputConnection(reader.GetOutputPort())
        magnify.SetMagnificationFactors(IMAGE_MAG_X, IMAGE_MAG_Y ,IMAGE_MAG_Z)

        imageCursor = svtk.svtkImageCursor3D()
        imageCursor.SetInputConnection(magnify.GetOutputPort())
        imageCursor.SetCursorPosition(
                CURSOR_X*IMAGE_MAG_X,
                CURSOR_Y*IMAGE_MAG_Y,
                CURSOR_Z*IMAGE_MAG_Z)
        imageCursor.SetCursorValue(255)
        imageCursor.SetCursorRadius(50*IMAGE_MAG_X)

        axes = svtk.svtkAxes()
        axes.SymmetricOn()
        axes.SetOrigin(CURSOR_X, CURSOR_Y, CURSOR_Z)
        axes.SetScaleFactor(50.0)

        axes_mapper = svtk.svtkPolyDataMapper()
        axes_mapper.SetInputConnection(axes.GetOutputPort())

        axesActor = svtk.svtkActor()
        axesActor.SetMapper(axes_mapper)
        axesActor.GetProperty().SetAmbient(0.5)

        # Image viewer stuff.
        viewer = svtk.svtkImageViewer()
        viewer.SetInputConnection(imageCursor.GetOutputPort())
        viewer.SetZSlice(CURSOR_Z*IMAGE_MAG_Z)
        viewer.SetColorWindow(256)
        viewer.SetColorLevel(128)

        # Create transfer functions for opacity and color.
        opacity_transfer_function = svtk.svtkPiecewiseFunction()
        opacity_transfer_function.AddPoint(20, 0.0)
        opacity_transfer_function.AddPoint(255, 0.2)

        color_transfer_function = svtk.svtkColorTransferFunction()
        color_transfer_function.AddRGBPoint(0, 0, 0, 0)
        color_transfer_function.AddRGBPoint(64, 1, 0, 0)
        color_transfer_function.AddRGBPoint(128, 0, 0, 1)
        color_transfer_function.AddRGBPoint(192, 0, 1, 0)
        color_transfer_function.AddRGBPoint(255, 0, .2, 0)

        # Create properties, mappers, volume actors, and ray cast function.
        volume_property = svtk.svtkVolumeProperty()
        volume_property.SetColor(color_transfer_function)
#         volume_property.SetColor(color_transfer_function[0],
#                                  color_transfer_function[1],
#                                  color_transfer_function[2])
        volume_property.SetScalarOpacity(opacity_transfer_function)

        volume_mapper = svtk.svtkFixedPointVolumeRayCastMapper()
        volume_mapper.SetInputConnection(reader.GetOutputPort())

        volume = svtk.svtkVolume()
        volume.SetMapper(volume_mapper)
        volume.SetProperty(volume_property)

        # Create outline.
        outline = svtk.svtkOutlineFilter()
        outline.SetInputConnection(reader.GetOutputPort())

        outline_mapper = svtk.svtkPolyDataMapper()
        outline_mapper.SetInputConnection(outline.GetOutputPort())

        outlineActor = svtk.svtkActor()
        outlineActor.SetMapper(outline_mapper)
        outlineActor.GetProperty().SetColor(1, 1, 1)

        # Create the renderer.
        ren = svtk.svtkRenderer()
        ren.AddActor(axesActor)
        ren.AddVolume(volume)
        ren.SetBackground(0.1, 0.2, 0.4)

        renWin = svtk.svtkRenderWindow()
        renWin.AddRenderer(ren)
        renWin.SetSize(256, 256)

        # Create the GUI: two renderer widgets and a quit button.
        self.root = tkinter.Tk()
        self.root.title("cursor3D")
        # Define what to do when the user explicitly closes a window.
        self.root.protocol("WM_DELETE_WINDOW", OnClosing)

        # Help label, frame and quit button
        helpLabel = tkinter.Label(self.root,
            text=
            "MiddleMouse (or shift-LeftMouse) in image viewer to place cursor")
        displayFrame = tkinter.Frame(self.root)
        quitButton = tkinter.Button(self.root, text= "Quit", command=OnClosing)

        # Pack the GUI.
        helpLabel.pack()
        displayFrame.pack(fill=BOTH, expand=TRUE)
        quitButton.pack(fill=X)

        # Create the viewer widget.
        viewerFrame = tkinter.Frame(displayFrame)
        viewerFrame.pack(padx=3, pady=3, side=LEFT, anchor=N,
                        fill=BOTH, expand=FALSE)
        self.tkvw = svtkTkImageViewerWidget(viewerFrame, iv=viewer,
                        width=264, height=264)
        viewerControls = tkinter.Frame(viewerFrame)
        viewerControls.pack(side=BOTTOM, anchor=S, fill=BOTH, expand=TRUE)
        self.tkvw.pack(side=TOP, anchor=N, fill=BOTH, expand=FALSE)
        downButton = tkinter.Button(viewerControls, text="Down",
                            command=[ViewerDown,self.tkvw])
        upButton = tkinter.Button(viewerControls, text="Up",
                            command=[ViewerUp,self.tkvw])
        sliceLabel = tkinter.Label(viewerControls,
                            text="slice: "+str(CURSOR_Z*IMAGE_MAG_Z))
        downButton.pack(side=LEFT, expand=TRUE, fill=BOTH)
        upButton.pack(side=LEFT, expand=TRUE, fill=BOTH)
        sliceLabel.pack(side=LEFT, expand=TRUE, fill=BOTH)

        # Create the render widget
        renderFrame = tkinter.Frame(displayFrame)
        renderFrame.pack(padx=3, pady=3, side=LEFT, anchor=N,
                        fill=BOTH, expand=TRUE)
        self.tkrw = svtkTkRenderWidget(renderFrame, rw=renWin,
                        width=264, height=264)

        self.tkrw.pack(side=TOP, anchor=N, fill=BOTH, expand=TRUE)

        # Bindings
        self.tkvw.BindTkImageViewer()
        self.tkrw.BindTkRenderWidget()

        # Lets add an extra binding of the middle button in the image viewer
        # to set the cursor location.
        self.tkvw.bind('<Button-2>',SetCursorFromViewer)
        self.tkvw.bind('<Shift-Button-1>',SetCursorFromViewer)

        # Associate the functions with the buttons and label.
        #
        downButton.config(command=partial(ViewerDown, self.tkvw))
        upButton.config(command=partial(ViewerUp, self.tkvw))

    def DoIt(self):
        self.SetUp()
        self.tkvw.Render()
        self.tkrw.Render()
        self.root.update()
        # If you want to interact and use the sliders etc,
        # uncomment the following line.
        #self.root.mainloop()
        img_file = "cursor3D.png"
        Testing.compareImage(self.tkvw.GetImageViewer().GetRenderWindow(),
                             Testing.getAbsImagePath(img_file))
        # Testing.interact()
        self.root.destroy()

if __name__ == '__main__':
    cases = [(Cursor3DViewer, 'DoIt')]
    del Cursor3DViewer
    Testing.main(cases)