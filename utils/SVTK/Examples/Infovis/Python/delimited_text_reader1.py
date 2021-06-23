#!/usr/bin/env python
from __future__ import print_function
from svtk import *

csv_source = svtkDelimitedTextReader()
csv_source.SetFieldDelimiterCharacters(",")
csv_source.SetHaveHeaders(True)
csv_source.SetDetectNumericColumns(True)
csv_source.SetFileName("authors.csv")
csv_source.Update()

T = csv_source.GetOutput()

print("Table loaded from CSV file:")
T.Dump(10)