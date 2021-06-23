#!/usr/bin/env python
from __future__ import print_function
from svtk import *
import os.path
from svtk.util.misc import svtkGetDataRoot
SVTK_DATA_ROOT = svtkGetDataRoot()

data_dir = SVTK_DATA_ROOT + "/Data/Infovis/SQLite/"
if not os.path.exists(data_dir):
  data_dir = SVTK_DATA_ROOT + "/Data/Infovis/SQLite/"
if not os.path.exists(data_dir):
  data_dir = SVTK_DATA_ROOT + "/Data/Infovis/SQLite/"
sqlite_file = data_dir + "temperatures.db"

# Pull the table from the database
databaseToTable = svtkSQLDatabaseTableSource()
databaseToTable.SetURL("sqlite://" + sqlite_file)
databaseToTable.SetQuery("select * from main_tbl")

# How many rows does the input table have?
databaseToTable.Update()
inputTable = databaseToTable.GetOutput()
numRows = inputTable.GetNumberOfRows()
print("Input Table Rows: " , numRows)

# Divide the table into 4
# Note1: using data specific method
subSize = numRows/4
leftOver = numRows - subSize*4
print("subSize: ", subSize)
print("leftOver: ", leftOver)

# Python knows nothing of enumerated types
# 6 = svtkDataObject::FIELD_ASSOCIATION_ROWS
# Mode = ACCEPT_LESS_THAN = 0, ACCEPT_GREATER_THAN = 1, ACCEPT_BETWEEN = 2, ACCEPT_OUTSIDE = 3
threshold = svtkThresholdTable()
threshold.SetInputConnection(databaseToTable.GetOutputPort())
threshold.SetInputArrayToProcess(0,0,0,6, "id")
threshold.SetMode(2)
threshold.SetMinValue(svtkVariant(0))
threshold.SetMaxValue(svtkVariant(subSize-1))
threshold.Update()
subTable1 = svtkTable()
subTable1.DeepCopy(threshold.GetOutput())
subTable1.Dump(10)

threshold.SetMinValue(svtkVariant(subSize))
threshold.SetMaxValue(svtkVariant(subSize*2-1))
threshold.Update()
subTable2 = svtkTable()
subTable2.DeepCopy(threshold.GetOutput())
subTable2.Dump(10)

threshold.SetMinValue(svtkVariant(subSize*2))
threshold.SetMaxValue(svtkVariant(subSize*3-1))
threshold.Update()
subTable3 = svtkTable()
subTable3.DeepCopy(threshold.GetOutput())

threshold.SetMinValue(svtkVariant(subSize*3))
threshold.SetMaxValue(svtkVariant(subSize*4+leftOver-1))
threshold.Update()
subTable4 = svtkTable()
subTable4.DeepCopy(threshold.GetOutput())
print("SubTable1 Rows: " , subTable1.GetNumberOfRows())
print("SubTable2 Rows: " , subTable2.GetNumberOfRows())
print("SubTable3 Rows: " , subTable3.GetNumberOfRows())
print("SubTable4 Rows: " , subTable4.GetNumberOfRows())


# Calculate offline(non-streaming) descriptive statistics
print("# Calculate offline descriptive statistics:")
ds = svtkDescriptiveStatistics()
ds.SetInputConnection(databaseToTable.GetOutputPort())
ds.AddColumn("Temp1")
ds.AddColumn("Temp2")
ds.Update()

dStats = ds.GetOutputDataObject( 1 )
dPrimary = dStats.GetBlock( 0 )
dDerived = dStats.GetBlock( 1 )
dPrimary.Dump( 15 )
dDerived.Dump( 15 )


inter = svtkDescriptiveStatistics()
inter.AddColumn("Temp1")
inter.AddColumn("Temp2")


# Calculate online(streaming) descriptive statistics
print("# Calculate online descriptive statistics:")
ss = svtkStreamingStatistics()
ss.SetStatisticsAlgorithm(inter)
ss.SetInputData(subTable1)
ss.Update()

ss.SetInputData(subTable2)
ss.Update()

ss.SetInputData(subTable3)
ss.Update()

ss.SetInputData(subTable4)
ss.Update()

sStats = ss.GetOutputDataObject( 1 )
sPrimary = sStats.GetBlock( 0 )
sDerived = sStats.GetBlock( 1 )
sPrimary.Dump( 15 )
sDerived.Dump( 15 )