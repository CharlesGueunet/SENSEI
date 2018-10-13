#include "Bridge.h"
#include "DataAdaptor.h"

#include <sensei/ConfigurableAnalysis.h>
#include <vtkNew.h>
#include <vtkDataObject.h>
#include <vector>

#include "Timer.h"

namespace BridgeInternals
{
  static vtkSmartPointer<parallel3d::DataAdaptor> GlobalDataAdaptor;
  static vtkSmartPointer<sensei::ConfigurableAnalysis> GlobalAnalysisAdaptor;
}

//-----------------------------------------------------------------------------
void bridge_initialize(MPI_Comm comm, int g_x, int g_y, int g_z,
  int l_x, int l_y, int l_z, uint64_t start_extents_x, uint64_t start_extents_y,
  uint64_t start_extents_z, int tot_blocks_x, int tot_blocks_y, int tot_blocks_z,
  int block_id_x, int block_id_y, int block_id_z, const char* config_file)
{
  timer::Initialize();

  BridgeInternals::GlobalDataAdaptor = vtkSmartPointer<parallel3d::DataAdaptor>::New();
  BridgeInternals::GlobalDataAdaptor->SetCommunicator(comm);

  BridgeInternals::GlobalDataAdaptor->Initialize(g_x, g_y, g_z, l_x, l_y, l_z,
    start_extents_x, start_extents_y, start_extents_z, tot_blocks_x,
    tot_blocks_y, tot_blocks_z, block_id_x, block_id_y, block_id_z);

  BridgeInternals::GlobalAnalysisAdaptor = vtkSmartPointer<sensei::ConfigurableAnalysis>::New();
  BridgeInternals::GlobalAnalysisAdaptor->SetCommunicator(comm);
  BridgeInternals::GlobalAnalysisAdaptor->Initialize(config_file);
}

//-----------------------------------------------------------------------------
void bridge_update(int tstep, double time, double *pressure, double* temperature, double* density)
{
  BridgeInternals::GlobalDataAdaptor->SetDataTime(time);
  BridgeInternals::GlobalDataAdaptor->SetDataTimeStep(tstep);
  BridgeInternals::GlobalDataAdaptor->AddArray("pressure", pressure);
  BridgeInternals::GlobalDataAdaptor->AddArray("temperature", temperature);
  BridgeInternals::GlobalDataAdaptor->AddArray("density", density);
  BridgeInternals::GlobalAnalysisAdaptor->Execute(BridgeInternals::GlobalDataAdaptor);
  BridgeInternals::GlobalDataAdaptor->ReleaseData();
}

//-----------------------------------------------------------------------------
void bridge_finalize()
{
  BridgeInternals::GlobalAnalysisAdaptor->Finalize();

  BridgeInternals::GlobalAnalysisAdaptor = nullptr;
  BridgeInternals::GlobalDataAdaptor = nullptr;

  timer::Finalize();
}
