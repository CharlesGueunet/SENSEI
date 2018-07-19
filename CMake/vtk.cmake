set(SENSEI_VTK_COMPONENTS vtkCommonDataModel)
if (ENABLE_VTK_MPI)
  list(APPEND SENSEI_VTK_COMPONENTS vtkParallelMPI)
endif()
if (ENABLE_VTK_IO)
  list(APPEND SENSEI_VTK_COMPONENTS vtkIOXML)
  if (ENABLE_VTK_MPI)
    list(APPEND SENSEI_VTK_COMPONENTS vtkIOParallelXML)
  endif()
endif()
if (ENABLE_PYTHON)
  list(APPEND SENSEI_VTK_COMPONENTS vtkPython vtkWrappingPythonCore)
endif()
if (ENABLE_VTK_M)
  list(APPEND SENSEI_VTK_COMPONENTS vtkAcceleratorsVTKm vtkIOLegacy
    vtkFiltersGeometry vtkImagingCore)

  # Now find VTK-m so it can be used independently of VTK
  # This makes the vtkm_cont library available as well as
  # targets vtkm::tbb and vtkm::cuda if they exist (so that
  # device support can be conditionally compiled) using
  #    IF (TARGET vtkm::tbb) ... ENDIF()
  find_package(VTKm REQUIRED QUIET)
endif()

if (NOT ENABLE_CATALYST)
  add_library(vtk INTERFACE)

  find_package(VTK QUIET COMPONENTS ${SENSEI_VTK_COMPONENTS})
  if (NOT VTK_FOUND)
    message(FATAL_ERROR "VTK (${SENSEI_VTK_COMPONENTS}) modules are required for "
      "Sensei core even when not using any infrastructures. Please set "
      "VTK_DIR to point to a directory containing `VTKConfig.cmake`.")
  endif()

  target_link_libraries(vtk INTERFACE ${VTK_LIBRARIES})
  target_include_directories(vtk SYSTEM INTERFACE ${VTK_INCLUDE_DIRS})
  target_compile_definitions(vtk INTERFACE ${VTK_DEFINITIONS})

  install(TARGETS vtk EXPORT vtk)
  install(EXPORT vtk DESTINATION lib/cmake EXPORT_LINK_INTERFACE_LIBRARIES)
endif()
