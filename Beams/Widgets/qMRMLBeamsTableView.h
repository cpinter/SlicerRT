/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Csaba Pinter, PerkLab, Queen's University
  and was supported through the Applied Cancer Research Unit program of Cancer Care
  Ontario with funds provided by the Ontario Ministry of Health and Long-Term Care

==============================================================================*/

#ifndef __qMRMLBeamsTableView_h
#define __qMRMLBeamsTableView_h

// Beams includes
#include "qSlicerBeamsModuleWidgetsExport.h"

// MRMLWidgets includes
#include "qMRMLWidget.h"

// CTK includes
#include <ctkPimpl.h>
#include <ctkVTKObject.h>

class qMRMLBeamsTableViewPrivate;
class vtkMRMLNode;
class vtkMRMLRTBeamNode;
class QTableWidgetItem;
class QItemSelection;

/// \ingroup SlicerRt_QtModules_Beams_Widgets
class Q_SLICER_MODULE_BEAMS_WIDGETS_EXPORT qMRMLBeamsTableView : public qMRMLWidget
{
  Q_OBJECT
  QVTK_OBJECT

  /// Weight column visibility property.
  Q_PROPERTY(bool weightColumnVisibility READ weightColumnVisibility WRITE setWeightColumnVisibility)
  /// Edit column visibility property.
  Q_PROPERTY(bool editColumnVisibility READ editColumnVisibility WRITE setEditColumnVisibility)
  /// Clone column visibility property.
  Q_PROPERTY(bool cloneColumnVisibility READ cloneColumnVisibility WRITE setCloneColumnVisibility)
  /// Visibility column visibility property.
  Q_PROPERTY(bool visibilityColumnVisibility READ visibilityColumnVisibility WRITE setVisibilityColumnVisibility)
  /// Beam's Eye View column visibility property.
  Q_PROPERTY(bool bevColumnVisibility READ bevColumnVisibility WRITE setBevColumnVisibility)

  enum BeamTableItemDataRole
  {
    /// Unique ID of the item. For nodes, it is the node ID.
    IDRole = Qt::UserRole + 1
  };

public:
  typedef qMRMLWidget Superclass;
  /// Constructor
  explicit qMRMLBeamsTableView(QWidget* parent = nullptr);
  /// Destructor
  ~qMRMLBeamsTableView() override;

  /// Get plan MRML node
  Q_INVOKABLE vtkMRMLNode* planNode();

  /// Return number of beams (rows) in the table
  Q_INVOKABLE int beamCount() const;

  /// Return selected beam node ID
  Q_INVOKABLE QStringList selectedBeamNodeIDs();

  /// Get visibility of the weight column in the beams table
  Q_INVOKABLE bool weightColumnVisibility()const;
  /// Get visibility of the edit column in the beams table
  Q_INVOKABLE bool editColumnVisibility()const;
  /// Get visibility of the clone column in the beams table
  Q_INVOKABLE bool cloneColumnVisibility()const;
  /// Get visibility of the visibility column in the beams table
  Q_INVOKABLE bool visibilityColumnVisibility()const;
  /// Get visibility of the Beam's Eye View column in the beams table
  Q_INVOKABLE bool bevColumnVisibility()const;

public slots:
  /// Set plan MRML node
  void setPlanNode(vtkMRMLNode* node);

  /// Called when beam is added in an observed plan node
  void onBeamAdded(vtkObject* caller, void* callData);
  /// Called when beam is removed in an observed plan node
  void onBeamRemoved(vtkObject* caller, void* callData);

  /// Set visibility of the weight column in the beams table
  void setWeightColumnVisibility(bool on);
  /// Set visibility of the edit column in the beams table
  void setEditColumnVisibility(bool on);
  /// Set visibility of the clone column in the beams table
  void setCloneColumnVisibility(bool on);
  /// Set visibility of the visibility column in the beams table
  void setVisibilityColumnVisibility(bool on);
  /// Set visibility of the Beam's Eye View column in the beams table
  void setBevColumnVisibility(bool on);

  /// Show beam's eye view for given beam in first 3D view.
  static void showBeamsEyeView(vtkMRMLRTBeamNode* beamNode, double elevationMm=0.0);

signals:
  /// Emitted if selection changes
  void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

protected slots:
  /// Handle changing of values in a cell
  void onBeamTableItemChanged(QTableWidgetItem* changedItem);

  /// Handle edit button click. Switches to Beams module and selects beam.
  void onEditButtonClicked();

  /// Handle clone button click. Creates clone of the beam for which the button was clicked.
  void onCloneButtonClicked();

  /// Handle visibility button click. Toggles beam visibility.
  void onVisibilityButtonClicked();

  /// Handle Beam's Eye View button click. Sets camera to show Beam's Eye View of the beam of the row.
  void onBevButtonClicked();

  /// Update beam table according to the plan node.
  void updateBeamTable();

  /// To prevent accidentally moving out of the widget when pressing up/down arrows.
  bool eventFilter(QObject* target, QEvent* event) override;

protected:
  QScopedPointer<qMRMLBeamsTableViewPrivate> d_ptr;

private:
  Q_DECLARE_PRIVATE(qMRMLBeamsTableView);
  Q_DISABLE_COPY(qMRMLBeamsTableView);
};

#endif
