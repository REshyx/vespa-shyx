#ifndef vtkSHYXRemeshProgressState_h
#define vtkSHYXRemeshProgressState_h

class vtkObject;

/**
 * Write the current ParaView session to %TEMP%/shyx_remesh_pre_apply.pvsm, leaving out the
 * filter that is calling this (and everything downstream of it): the saved state is the
 * pipeline from before the Apply, so loading it cannot rerun the remesh.
 * No-op without an active proxy manager (pure VTK). Failure does not abort remesh.
 */
void vtkSHYXSaveRemeshProgressState(vtkObject* self);

#endif
