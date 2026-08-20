#ifndef vtkSHYXRemeshProgressState_h
#define vtkSHYXRemeshProgressState_h

class vtkObject;

/** Write the current ParaView session to %TEMP%/shyx_remesh_pre_apply.pvsm.
 *  No-op without an active proxy manager (pure VTK). Failure does not abort remesh. */
void vtkSHYXSaveRemeshProgressState(vtkObject* self);

#endif
