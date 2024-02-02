// -*- c++ -*-
/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkCGALXYZReader.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

/**
 * @class   vtkCGALXYZReader
 *
 *
 *
 * A superclass for reading AIM files.  Subclass add conventions to the
 * reader.  This class just outputs data into a multi block data set with a
 * vtkImageData at each block.  A block is created for each variable except that
 * variables with matching dimensions will be placed in the same block.
 */

#ifndef vtkCGALXYZReader_h
#define vtkCGALXYZReader_h

#include "vtkCGALPMPModule.h" // For export macro
#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkDataArraySelection.h" // for ivars
#include <string>                  //For std::string

class vtkDataArraySelection;
class vtkDataSet;
class vtkDoubleArray;
class vtkIntArray;
class vtkStdString;
class vtkStringArray;
class vtkAIMToolsPrivate;

class VTKCGALPMP_EXPORT vtkCGALXYZReader : public vtkCGALPolyDataAlgorithm
{
public:
  vtkTypeMacro(vtkCGALXYZReader, vtkCGALPolyDataAlgorithm);
  static vtkCGALXYZReader *New();
  void PrintSelf(ostream &os, vtkIndent indent) override;

  virtual void SetFileName(VTK_FILEPATH const char *filename);
  vtkGetFilePathMacro(FileName);

protected:
  vtkCGALXYZReader();
  ~vtkCGALXYZReader() override;

  int RequestData(vtkInformation *request, vtkInformationVector **inputVector,
                  vtkInformationVector *outputVector) override;

  int FillOutputPortInformation(int port, vtkInformation *info) override;

private:
  vtkCGALXYZReader(const vtkCGALXYZReader &) = delete;
  void operator=(const vtkCGALXYZReader &) = delete;

  char *FileName;
};

#endif // vtkCGALXYZReader_h
