/**
 * @class   vtkSHYXEMeshReader
 * @brief   Read OpenFOAM featureEdgeMesh / extendedFeatureEdgeMesh files as line vtkPolyData.
 *
 * Parses ASCII FoamFile class featureEdgeMesh, edgeMesh, extendedFeatureEdgeMesh, or
 * extendedEdgeMesh (typical extensions .eMesh / .extendedFeatureEdgeMesh). Gzip-compressed
 * files are rejected (no VTK::zlib, so official ParaView does not need vtkzlib DLL).
 * Binary Foam format is also rejected.
 *
 * Output is vtkPolyData lines. When the file is extendedFeatureEdgeMesh, cell/point arrays
 * and field data match vtkSHYXExtendedFeatureEdgeMesh so the result can feed
 * SHYXSnappyHexMesh Feature edges (including FoamExtendedFeatureEdgeMesh).
 *
 * Does not link OpenFOAM; no VESPA_USE_SNAPPYHEXMESH required.
 */

#ifndef vtkSHYXEMeshReader_h
#define vtkSHYXEMeshReader_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXEMeshReaderModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXEMESHREADER_EXPORT vtkSHYXEMeshReader : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXEMeshReader* New();
  vtkTypeMacro(vtkSHYXEMeshReader, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetFilePathMacro(FileName);
  vtkGetFilePathMacro(FileName);

  /** 1 if the path looks like an ASCII OpenFOAM eMesh / extendedFeatureEdgeMesh. */
  static int CanReadFile(const char* fname);

protected:
  vtkSHYXEMeshReader();
  ~vtkSHYXEMeshReader() override;

  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  char* FileName;

private:
  vtkSHYXEMeshReader(const vtkSHYXEMeshReader&) = delete;
  void operator=(const vtkSHYXEMeshReader&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
