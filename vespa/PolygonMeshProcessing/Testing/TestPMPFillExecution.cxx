#include <iostream>

#include <vtkDelimitedTextReader.h>
#include <vtkInformation.h>
#include <vtkNew.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkTable.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALPatchFilling.h"

int TestPMPFillExecution(int, char* argv[])
{
  // Open data

  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/hand.vtp";
  reader->SetFileName(cfname.c_str());

  // Create point selection

  vtkNew<vtkSelection>     sel;
  vtkNew<vtkSelectionNode> node;
  sel->AddNode(node);
  node->GetProperties()->Set(vtkSelectionNode::CONTENT_TYPE(), vtkSelectionNode::INDICES);
  node->GetProperties()->Set(vtkSelectionNode::FIELD_TYPE(), vtkSelectionNode::POINT);

  // Read selection
  vtkNew<vtkDelimitedTextReader> roiReader;
  std::string                    roiFilename(argv[1]);
  roiFilename += "/FingerSelection.csv";
  roiReader->SetFileName(roiFilename.c_str());
  roiReader->SetHaveHeaders(true);
  roiReader->SetDetectNumericColumns(true);
  roiReader->Update();

  vtkIntArray* roiArr =
    vtkIntArray::SafeDownCast(roiReader->GetOutput()->GetColumn(0));

  vtkNew<vtkIdTypeArray> arr;
  arr->SetNumberOfTuples(roiArr->GetNumberOfTuples());

  for (vtkIdType idx = 0; idx < roiArr->GetNumberOfTuples(); ++idx)
  {
    arr->SetValue(idx, roiArr->GetValue(idx));
  }

  node->SetSelectionList(arr);

  // Fill selected region

  vtkNew<vtkCGALPatchFilling> pf;
  pf->SetInputConnection(0, reader->GetOutputPort());
  pf->SetInputData(1, sel);

  // Save result

  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(pf->GetOutputPort());
  writer->SetFileName("fill_tunnels.vtp");
  writer->Write();

  return 0;
}
