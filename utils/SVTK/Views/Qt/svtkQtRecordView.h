/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkQtRecordView.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/
/**
 * @class   svtkQtRecordView
 * @brief   Superclass for QAbstractItemView-based views.
 *
 *
 * This superclass provides all the plumbing to integrate a QAbstractItemView
 * into the SVTK view framework, including reporting selection changes and
 * detecting selection changes from linked views.
 *
 * @par Thanks:
 * Thanks to Brian Wylie from Sandia National Laboratories for implementing
 * this class
 */

#ifndef svtkQtRecordView_h
#define svtkQtRecordView_h

#include "svtkQtView.h"
#include "svtkSmartPointer.h"  // Needed for data table member
#include "svtkViewsQtModule.h" // For export macro
#include <QPointer>           // Needed for the text widget member

class QTextEdit;
class svtkDataObjectToTable;

class SVTKVIEWSQT_EXPORT svtkQtRecordView : public svtkQtView
{
  Q_OBJECT

public:
  static svtkQtRecordView* New();
  svtkTypeMacro(svtkQtRecordView, svtkQtView);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  /**
   * Get the main container of this view (a  QWidget).
   * The application typically places the view with a call
   * to GetWidget(): something like this
   * this->ui->box->layout()->addWidget(this->View->GetWidget());
   */
  QWidget* GetWidget() override;

  enum
  {
    FIELD_DATA = 0,
    POINT_DATA = 1,
    CELL_DATA = 2,
    VERTEX_DATA = 3,
    EDGE_DATA = 4,
    ROW_DATA = 5,
  };

  //@{
  /**
   * The field type to copy into the output table.
   * Should be one of FIELD_DATA, POINT_DATA, CELL_DATA, VERTEX_DATA, EDGE_DATA.
   */
  svtkGetMacro(FieldType, int);
  void SetFieldType(int);
  //@}

  svtkGetMacro(CurrentRow, int);
  svtkGetStringMacro(Text);

  /**
   * Updates the view.
   */
  void Update() override;

protected:
  svtkQtRecordView();
  ~svtkQtRecordView() override;

  void AddRepresentationInternal(svtkDataRepresentation* rep) override;
  void RemoveRepresentationInternal(svtkDataRepresentation* rep) override;

  svtkSmartPointer<svtkDataObjectToTable> DataObjectToTable;

  QPointer<QTextEdit> TextWidget;

  char* Text;
  int FieldType;
  int CurrentRow;

private:
  svtkQtRecordView(const svtkQtRecordView&) = delete;
  void operator=(const svtkQtRecordView&) = delete;

  svtkMTimeType CurrentSelectionMTime;
  svtkMTimeType LastInputMTime;
  svtkMTimeType LastMTime;
};

#endif