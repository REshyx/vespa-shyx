#ifndef pqSHYXSkeletonExtractionAutoStart_h
#define pqSHYXSkeletonExtractionAutoStart_h

#include <QObject>
#include <QSet>

class pqPipelineFilter;
class pqPipelineSource;

/**
 * After the first Apply of SHYX Skeleton Extraction, keep the parent mesh
 * visible and set its representation Opacity to 0.5 (XML also sets
 * replace_input="0" so ParaView does not hide the input).
 *
 * Must hook filterCreated, not sourceAdded: sourceAdded fires while the
 * proxy is still UNMODIFIED; UNINITIALIZED is set afterwards.
 */
class pqSHYXSkeletonExtractionAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXSkeletonExtractionAutoStart(QObject* parent = nullptr);
  ~pqSHYXSkeletonExtractionAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  void watchFilter(pqPipelineSource* source);
  void fadeInputRepresentations(pqPipelineFilter* filter);

  QSet<pqPipelineFilter*> Pending;

  Q_DISABLE_COPY(pqSHYXSkeletonExtractionAutoStart)
};

#endif
