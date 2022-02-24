#include <iostream>

#include <vtkInformation.h>
#include <vtkNew.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALIsotropicRemesher.h"
#include "vtkCGALFairRegion.h"

int TestPMPExecution(int, char* argv[])
{
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Remesh

  vtkNew<vtkCGALIsotropicRemesher> rm;
  rm->SetInputConnection(reader->GetOutputPort());
  rm->SetIterations(3);

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(rm->GetOutputPort());
  writer->SetFileName("isotropic_remesh.vtp");
  writer->Update();
  writer->Write();

  // Fair

  // With point selection
  vtkNew<vtkSelection>     sel;
  vtkNew<vtkSelectionNode> node;
  sel->AddNode(node);
  node->GetProperties()->Set(vtkSelectionNode::CONTENT_TYPE(), vtkSelectionNode::INDICES);
  node->GetProperties()->Set(vtkSelectionNode::FIELD_TYPE(), vtkSelectionNode::POINT);

  // list of cells to be selected
  vtkNew<vtkIdTypeArray> arr;
  arr->SetNumberOfTuples(17);

  arr->SetTuple1(0, 37797);
  arr->SetTuple1(1, 37798);
  arr->SetTuple1(2, 37799);
  arr->SetTuple1(3, 37810);
  arr->SetTuple1(4, 37816);
  arr->SetTuple1(5, 37819);
  arr->SetTuple1(6, 37823);
  arr->SetTuple1(7, 37832);
  arr->SetTuple1(8, 37833);
  arr->SetTuple1(9, 37837);
  arr->SetTuple1(10, 37840);
  arr->SetTuple1(11, 37846);
  arr->SetTuple1(12, 37847);
  arr->SetTuple1(13, 37859);
  arr->SetTuple1(14, 37861);
  arr->SetTuple1(15, 37864);
  arr->SetTuple1(16, 37865);

  node->SetSelectionList(arr);

  vtkNew<vtkCGALFairRegion> fr;
  fr->SetInputConnection(0, reader->GetOutputPort());
  fr->SetInputData(1, sel);

  writer->SetInputConnection(fr->GetOutputPort());
  writer->SetFileName("fair_points.vtp");
  writer->Update();
  writer->Write();

  return 0;
}
