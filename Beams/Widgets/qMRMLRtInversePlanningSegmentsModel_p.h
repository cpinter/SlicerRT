/*==============================================================================

  Copyright (c) Ebatinca S.L., Las Palmas de Gran Canaria, Spain

  Licensed under the Apache License, Version 2.0 (the "License"); you may
  not use this file except in compliance with the License. You may obtain
  a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#ifndef __qMRMLRtInversePlanningSegmentsModel_p_h
#define __qMRMLRtInversePlanningSegmentsModel_p_h

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Slicer API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

// Qt includes
#include <QFlags>
#include <QMap>

// Segmentations includes
#include "qSlicerSegmentationsModuleWidgetsExport.h"

#include "qMRMLRtInversePlanningSegmentsModel.h"

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLSegmentationNode.h>

// VTK includes
#include <vtkCallbackCommand.h>
#include <vtkSmartPointer.h>

class QStandardItemModel;

//------------------------------------------------------------------------------
// qMRMLRtInversePlanningSegmentsModelPrivate
//------------------------------------------------------------------------------
class Q_SLICER_MODULE_SEGMENTATIONS_WIDGETS_EXPORT qMRMLRtInversePlanningSegmentsModelPrivate
{
  Q_DECLARE_PUBLIC(qMRMLRtInversePlanningSegmentsModel);

protected:
  qMRMLRtInversePlanningSegmentsModel* const q_ptr;
public:
  qMRMLRtInversePlanningSegmentsModelPrivate(qMRMLRtInversePlanningSegmentsModel& object);
  virtual ~qMRMLRtInversePlanningSegmentsModelPrivate();
  void init();

  // Insert a segment into the specified row
  // If no row is specified, then the index is retrieved from the segmentation
  QStandardItem* insertSegment(QString segmentID, int row=-1);

  /// Get string to pass terminology information via table widget item
  QString getTerminologyUserDataForSegment(vtkSegment* segment);

public:
  vtkSmartPointer<vtkCallbackCommand> CallBack;
  bool UpdatingItemFromSegment;

  int NameColumn;
  int VisibilityColumn;
  int ColorColumn;
  int OpacityColumn;
  int LayerColumn;

  QIcon VisibleIcon;
  QIcon HiddenIcon;

  /// Segmentation node
  vtkSmartPointer<vtkMRMLSegmentationNode> SegmentationNode;
};

#endif
