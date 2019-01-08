#ifndef sensei_PlanarPartitioner_h
#define sensei_PlanarPartitioner_h

#include "Partitioner.h"

namespace sensei
{
/// @class PlanarPartitioner
/// @brief PlanarPartitioner is class that represents the planar partitioning mode for in-transit operation.
///
/// In planar partitioning mode the blocks are distributed in blocks of a specified size. The size is 
///	specified in the 'plane_size' attribute. Note block is a special case of planar with a plane_size of 1.
class PlanarPartitioner : public sensei::Partitioner 
{
public:
  PlanarPartitioner(unsigned int plane_size);
  ~PlanarPartitioner();

  PlanarPartitioner(const PlanarPartitioner&) = delete;
  void operator=(const PlanarPartitioner&) = delete;

  int GetPartition(sensei::MeshMetadataPtr &remote, sensei::MeshMetadataPtr &local);
};

}
#endif
