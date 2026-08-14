#include <vtkDataArray.h>
#include <vtkDelimitedTextReader.h>
#include <vtkInformation.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkTable.h>
#include <vtkTestUtilities.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>

#include "vtkCGALMeshDeformation.h"

int TestPMPDeformExecution(int, char* argv[])
{
  // Open data
  vtkNew<vtkXMLPolyDataReader> reader;
  std::string                  cfname(argv[1]);
  cfname += "/dragon.vtp";
  reader->SetFileName(cfname.c_str());

  // Create point set for target positions of the control points
  vtkNew<vtkPointSet> targets;

  // Define global point IDs of the control points
  vtkNew<vtkIntArray> ctrlPointIds;
  ctrlPointIds->SetName("vtkOriginalPointIds");
  ctrlPointIds->SetNumberOfTuples(2);
  ctrlPointIds->SetValue(0, 42868);
  ctrlPointIds->SetValue(1, 48903);
  targets->GetPointData()->AddArray(ctrlPointIds);

  // Move the X coordinates by -0.2
  vtkNew<vtkPoints> targetPoints;
  targetPoints->SetNumberOfPoints(2);
  targetPoints->SetPoint(0, 1.47875, 2.01455, -0.040048);
  targetPoints->SetPoint(1, 1.48846, 2.01667, -0.0417898);
  targets->SetPoints(targetPoints);

  // Create point selection for the ROI
  vtkNew<vtkSelection>     sel;
  vtkNew<vtkSelectionNode> node;
  sel->AddNode(node);
  node->GetProperties()->Set(vtkSelectionNode::CONTENT_TYPE(), vtkSelectionNode::INDICES);
  node->GetProperties()->Set(vtkSelectionNode::FIELD_TYPE(), vtkSelectionNode::POINT);

  // Read ROI points IDs from CSV file
  vtkNew<vtkDelimitedTextReader> roiReader;
  std::string                    roiFilename(argv[1]);
  roiFilename += "/HornSelection.csv";
  roiReader->SetFileName(roiFilename.c_str());
  roiReader->SetHaveHeaders(true);
  roiReader->SetDetectNumericColumns(true);
  roiReader->Update();

  vtkIntArray* roiArr =
    vtkIntArray::SafeDownCast(roiReader->GetOutput()->GetColumnByName("vtkOriginalPointIds"));

  vtkNew<vtkIdTypeArray> arr;
  arr->SetNumberOfTuples(roiArr->GetNumberOfTuples());

  for (vtkIdType idx = 0; idx < roiArr->GetNumberOfTuples(); ++idx)
  {
    arr->SetValue(idx, roiArr->GetValue(idx));
  }

  node->SetSelectionList(arr);

  // Deform selected ROI using target positions
  vtkNew<vtkCGALMeshDeformation> deformer;
  deformer->SetInputConnection(0, reader->GetOutputPort());
  deformer->SetInputData(1, targets);
  deformer->SetInputData(2, sel);
  deformer->SetGlobalIdArray("vtkOriginalPointIds");

  // Save result
  vtkNew<vtkXMLPolyDataWriter> writer;
  writer->SetInputConnection(deformer->GetOutputPort());
  writer->SetFileName("deform_dragon.vtp");
  writer->Write();

  return 0;
}
