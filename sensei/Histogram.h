#ifndef sensei_Histogram_h
#define sensei_Histogram_h

#include "AnalysisAdaptor.h"
#include <mpi.h>
#include <vector>

class svtkDataObject;
class svtkDataArray;

namespace sensei
{

class SVTKHistogram;

/// @class Histogram
/// @brief Computes a parallel histogram
class Histogram : public AnalysisAdaptor
{
public:
  static Histogram* New();
  senseiTypeMacro(Histogram, AnalysisAdaptor);

  void Initialize(int bins, const std::string &meshName,
    int association, const std::string& arrayName,
    const std::string &fileName);

  bool Execute(DataAdaptor* data) override;

  int Finalize() override;

  // return the last computed histogram
  int GetHistogram(double &min, double &max,
    std::vector<unsigned int> &bins);

protected:
  Histogram();
  ~Histogram();

  Histogram(const Histogram&) = delete;
  void operator=(const Histogram&) = delete;

  static const char *GetGhostArrayName();
  svtkDataArray* GetArray(svtkDataObject* dobj, const std::string& arrayname);

  int Bins;
  std::string MeshName;
  std::string ArrayName;
  int Association;
  std::string FileName;

  SVTKHistogram *Internals;

};

}

#endif
