# ZFP for for SVTK

This branch contains changes required to embed the ZFP library into SVTK. This
includes changes made primarily to the build system to allow it to be embedded
into another source tree as well as a header to facilitate mangling of the
symbols to avoid conflicts with other copies of the library within a single
process.

  * Add attributes to pass commit checks within SVTK.
  * Add a CMake build system to the project.
  * Mangle all exported symbols to have a `svtkzfp_` prefix.