#include <iostream>

#include <vtkInformation.h>
#include <vtkNew.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALTunnelFilling.h"

int TestPMPFillExecution(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Create point selection

  vtkNew<vtkSelection>     sel;
  vtkNew<vtkSelectionNode> node;
  sel->AddNode(node);
  node->GetProperties()->Set(vtkSelectionNode::CONTENT_TYPE(), vtkSelectionNode::INDICES);
  node->GetProperties()->Set(vtkSelectionNode::FIELD_TYPE(), vtkSelectionNode::POINT);

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

  // Fair selected region

  vtkNew<vtkCGALTunnelFilling> tf;
  tf->SetInputConnection(0, reader->GetOutputPort());
  tf->SetInputData(1, sel);

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(tf->GetOutputPort());
  writer->SetFileName("fill_tunnels.vtp");
  writer->Update();
  writer->Write();

  return 0;
}
