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

// Segmentations includes
#include "qMRMLSegmentsModel.h"
#include "qMRMLRtInversePlanningSegmentsTableView.h"
#include "qMRMLSortFilterSegmentsProxyModel.h"
#include "ui_qMRMLRtInversePlanningSegmentsTableView.h"
#include "vtkMRMLSegmentationNode.h"
#include "vtkMRMLSegmentationDisplayNode.h"
#include "vtkSegmentation.h"
#include "vtkSegment.h"

// Segmentations logic includes
#include "vtkSlicerSegmentationsModuleLogic.h"

// Terminologies includes
#include "qSlicerTerminologyItemDelegate.h"

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLLabelMapVolumeNode.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLSliceLogic.h>
#include <vtkMRMLSliceNode.h>

// Slicer includes
#include <qSlicerApplication.h>
#include <qSlicerCoreApplication.h>
#include <qSlicerLayoutManager.h>
#include <qSlicerModuleManager.h>
#include <qSlicerAbstractCoreModule.h>
#include <qMRMLItemDelegate.h>
#include <qMRMLSliceWidget.h>

// VTK includes
#include <vtkWeakPointer.h>

// Qt includes
#include <QAction>
#include <QContextMenuEvent>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QStringList>
#include <QTimer>
#include <QToolButton>

#define ID_PROPERTY "ID"
#define VISIBILITY_PROPERTY "Visible"

//-----------------------------------------------------------------------------
struct SegmentListFilterParameters
{
  QString TextFilter;

  const char AttributeSeparator = ';';
  const char KeyValueSeparator = ':';
  const char ValueSeparator = ',';

  const char* TextFilterKey = "text";

  SegmentListFilterParameters()
  {
    this->init();
  }

  void init()
  {
    this->TextFilter = "";
  }
};

//-----------------------------------------------------------------------------
class qMRMLRtInversePlanningSegmentsTableViewPrivate: public Ui_qMRMLRtInversePlanningSegmentsTableView
{
  Q_DECLARE_PUBLIC(qMRMLRtInversePlanningSegmentsTableView);

protected:
  qMRMLRtInversePlanningSegmentsTableView* const q_ptr;
public:
  qMRMLRtInversePlanningSegmentsTableViewPrivate(qMRMLRtInversePlanningSegmentsTableView& object);
  void init();

  /// Sets table message and takes care of the visibility of the label
  void setMessage(const QString& message);

public:
  /// Segmentation MRML node containing shown segments
  vtkWeakPointer<vtkMRMLSegmentationNode> SegmentationNode;

  /// Flag determining whether the long-press per-view segment visibility options are available
  bool AdvancedSegmentVisibility;

  /// Currently, if we are requesting segment display information from the
  /// segmentation display node,  the display node may emit modification events.
  /// We make sure these events do not interrupt the update process by setting
  /// IsUpdatingWidgetFromMRML to true when an update is already in progress.
  bool IsUpdatingWidgetFromMRML;

  bool IsFilterBarVisible;

  qMRMLSegmentsModel* Model;
  qMRMLSortFilterSegmentsProxyModel* SortFilterModel;

  QTimer FilterParameterChangedTimer;

  bool JumpToSelectedSegmentEnabled;

  /// When the model is being reset, the blocking state and selected segment IDs are stored here.
  bool WasBlockingTableSignalsBeforeReset;
  QStringList SelectedSegmentIDsBeforeReset;
};

//-----------------------------------------------------------------------------
qMRMLRtInversePlanningSegmentsTableViewPrivate::qMRMLRtInversePlanningSegmentsTableViewPrivate(qMRMLRtInversePlanningSegmentsTableView& object)
  : q_ptr(&object)
  , SegmentationNode(nullptr)
  , AdvancedSegmentVisibility(false)
  , IsUpdatingWidgetFromMRML(false)
  , IsFilterBarVisible(false)
  , Model(nullptr)
  , SortFilterModel(nullptr)
  , JumpToSelectedSegmentEnabled(false)
  , WasBlockingTableSignalsBeforeReset(false)
{
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableViewPrivate::init()
{
  Q_Q(qMRMLRtInversePlanningSegmentsTableView);

  this->setupUi(q);

  this->Model = new qMRMLSegmentsModel(this->SegmentsTable);
  this->SortFilterModel = new qMRMLSortFilterSegmentsProxyModel(this->SegmentsTable);
  this->SortFilterModel->setSourceModel(this->Model);
  this->SegmentsTable->setModel(this->SortFilterModel);

  // Hide filter bar to simplify default GUI. User can enable to handle many segments
  q->setFilterBarVisible(false);

  // Hide layer column
  q->setLayerColumnVisible(false);

  this->setMessage(QString());

  this->SegmentsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  this->SegmentsTable->horizontalHeader()->setSectionResizeMode(this->Model->nameColumn(), QHeaderView::Stretch);
  this->SegmentsTable->horizontalHeader()->setStretchLastSection(false);
  this->SegmentsTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

  // Select rows
  this->SegmentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  // Unset read-only by default (edit triggers are double click and edit key press)
  q->setReadOnly(false);

  // Setup filter parameter changed timer
  this->FilterParameterChangedTimer.setInterval(500);
  this->FilterParameterChangedTimer.setSingleShot(true);

  // Make connections
  QObject::connect(&this->FilterParameterChangedTimer, &QTimer::timeout, q, &qMRMLRtInversePlanningSegmentsTableView::updateMRMLFromFilterParameters);
  QObject::connect(this->SegmentsTable->selectionModel(), &QItemSelectionModel::selectionChanged, q, &qMRMLRtInversePlanningSegmentsTableView::onSegmentSelectionChanged);
  QObject::connect(this->Model, &qMRMLSegmentsModel::segmentAboutToBeModified, q, &qMRMLRtInversePlanningSegmentsTableView::segmentAboutToBeModified);
  QObject::connect(this->Model, &QAbstractItemModel::modelAboutToBeReset, q, &qMRMLRtInversePlanningSegmentsTableView::modelAboutToBeReset);
  QObject::connect(this->Model, &QAbstractItemModel::modelReset, q, &qMRMLRtInversePlanningSegmentsTableView::modelReset);
  QObject::connect(this->SegmentsTable, &QTableView::clicked, q, &qMRMLRtInversePlanningSegmentsTableView::onSegmentsTableClicked);
  QObject::connect(this->FilterLineEdit, &ctkSearchBox::textEdited, this->SortFilterModel, &qMRMLSortFilterSegmentsProxyModel::setTextFilter);
  QObject::connect(this->SortFilterModel, &qMRMLSortFilterSegmentsProxyModel::filterModified, q, &qMRMLRtInversePlanningSegmentsTableView::onSegmentsFilterModified);

  // Set item delegate to handle color and opacity changes
  this->SegmentsTable->setItemDelegateForColumn(this->Model->colorColumn(), new qSlicerTerminologyItemDelegate(this->SegmentsTable));
  this->SegmentsTable->setItemDelegateForColumn(this->Model->opacityColumn(), new qMRMLItemDelegate(this->SegmentsTable));
  this->SegmentsTable->installEventFilter(q);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableViewPrivate::setMessage(const QString& message)
{
  this->SegmentsTableMessageLabel->setVisible(!message.isEmpty());
  this->SegmentsTableMessageLabel->setText(message);
}

//-----------------------------------------------------------------------------
// qMRMLRtInversePlanningSegmentsTableView methods

//-----------------------------------------------------------------------------
qMRMLRtInversePlanningSegmentsTableView::qMRMLRtInversePlanningSegmentsTableView(QWidget* _parent)
  : qMRMLWidget(_parent)
  , d_ptr(new qMRMLRtInversePlanningSegmentsTableViewPrivate(*this))
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->init();
}

//-----------------------------------------------------------------------------
qMRMLRtInversePlanningSegmentsTableView::~qMRMLRtInversePlanningSegmentsTableView() = default;

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setSegmentationNode(vtkMRMLNode* node)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  vtkMRMLSegmentationNode* segmentationNode = vtkMRMLSegmentationNode::SafeDownCast(node);
  d->SegmentationNode = segmentationNode;
  d->Model->setSegmentationNode(d->SegmentationNode);

  // Connect segment added/removed and display modified events to population of the table
  qvtkReconnect(d->SegmentationNode, segmentationNode, vtkSegmentation::SegmentAdded,
    this, SLOT(onSegmentAddedOrRemoved()));
  qvtkReconnect(d->SegmentationNode, segmentationNode, vtkSegmentation::SegmentRemoved,
    this, SLOT(onSegmentAddedOrRemoved()));
  qvtkReconnect(d->SegmentationNode, segmentationNode, vtkCommand::ModifiedEvent,
    this, SLOT(updateWidgetFromMRML()));
  this->onSegmentAddedOrRemoved();
  this->updateWidgetFromMRML();
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onSegmentsFilterModified()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  QString textFilter = d->SortFilterModel->textFilter();
  if (d->FilterLineEdit->text() != textFilter)
  {
    d->FilterLineEdit->setText(textFilter);
  }

  if (d->SegmentationNode && !d->IsUpdatingWidgetFromMRML)
  {
    d->FilterParameterChangedTimer.start();
  }
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::updateMRMLFromFilterParameters()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << "Invalid segmentation node";
    return;
  }

  SegmentListFilterParameters filterParameters;
  filterParameters.TextFilter = d->SortFilterModel->textFilter();

  MRMLNodeModifyBlocker blocker(d->SegmentationNode);
  d->SegmentationNode->SetSegmentListFilterEnabled(d->IsFilterBarVisible);
  // std::string filterString = filterParameters.serializeStatusFilter().toStdString();
  // d->SegmentationNode->SetSegmentListFilterOptions(filterString);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onSegmentsTableClicked(const QModelIndex& modelIndex)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  QString segmentId = d->SortFilterModel->segmentIDFromIndex(modelIndex);
  QStandardItem* item = d->Model->itemFromSegmentID(segmentId);
  if (!d->SegmentationNode || !item)
  {
    return;
  }

  Qt::ItemFlags flags = item->flags();
  if (!flags.testFlag(Qt::ItemIsSelectable))
  {
    return;
  }

  vtkSegment* segment = d->SegmentationNode->GetSegmentation()->GetSegment(segmentId.toStdString());
  if (modelIndex.column() == d->Model->visibilityColumn())
  {
    // Set all visibility types to segment referenced by button toggled
    int visible = !item->data(qMRMLSegmentsModel::VisibilityRole).toInt();
    this->setSegmentVisibility(segmentId, visible, -1, -1, -1);
  }
}

//---------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onSegmentAddedOrRemoved()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  if (!d->SegmentationNode)
  {
    d->setMessage(tr("No node is selected"));
    return;
  }
  else if (d->SegmentationNode->GetSegmentation()->GetNumberOfSegments() == 0)
  {
    d->setMessage(tr("Empty segmentation"));
    return;
  }
  d->setMessage(QString());
}

//---------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::updateWidgetFromMRML()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    return;
  }

  bool wasUpdatingFromMRML = d->IsUpdatingWidgetFromMRML;
  d->IsUpdatingWidgetFromMRML = true;

  bool listFilterEnabled = d->SegmentationNode->GetSegmentListFilterEnabled();
  this->setFilterBarVisible(listFilterEnabled);

  QString filterOptions = QString::fromStdString(d->SegmentationNode->GetSegmentListFilterOptions());

  d->IsUpdatingWidgetFromMRML = wasUpdatingFromMRML;
}

//---------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setMRMLScene(vtkMRMLScene* newScene)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (newScene == this->mrmlScene())
  {
    return;
  }

  if (d->SegmentationNode && newScene != d->SegmentationNode->GetScene())
  {
    this->setSegmentationNode(nullptr);
  }

  Superclass::setMRMLScene(newScene);
}

//-----------------------------------------------------------------------------
vtkMRMLNode* qMRMLRtInversePlanningSegmentsTableView::segmentationNode()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  return d->SegmentationNode;
}

//--------------------------------------------------------------------------
qMRMLSortFilterSegmentsProxyModel* qMRMLRtInversePlanningSegmentsTableView::sortFilterProxyModel()const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SortFilterModel)
  {
    qCritical() << Q_FUNC_INFO << ": Invalid sort filter proxy model";
    return nullptr;
  }
  return d->SortFilterModel;
}

//--------------------------------------------------------------------------
qMRMLSegmentsModel* qMRMLRtInversePlanningSegmentsTableView::model()const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);
  if (!d->Model)
  {
    qCritical() << Q_FUNC_INFO << ": Invalid data model";
    return nullptr;
  }
  return d->Model;
}

//-----------------------------------------------------------------------------
QTableView* qMRMLRtInversePlanningSegmentsTableView::tableWidget()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return d->SegmentsTable;
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onSegmentSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (d->JumpToSelectedSegmentEnabled)
  {
    this->jumpSlices();
  }
  if (d->SegmentsTable->signalsBlocked())
  {
    return;
  }
  emit selectionChanged(selected, deselected);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onVisibility3DActionToggled(bool visible)
{
  QAction* senderAction = qobject_cast<QAction*>(sender());
  if (!senderAction)
  {
    return;
  }

  // Set 3D visibility to segment referenced by action toggled
  this->setSegmentVisibility(senderAction, -1, visible, -1, -1);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onVisibility2DFillActionToggled(bool visible)
{
  QAction* senderAction = qobject_cast<QAction*>(sender());
  if (!senderAction)
  {
    return;
  }

  // Set 2D fill visibility to segment referenced by action toggled
  this->setSegmentVisibility(senderAction, -1, -1, visible, -1);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::onVisibility2DOutlineActionToggled(bool visible)
{
  QAction* senderAction = qobject_cast<QAction*>(sender());
  if (!senderAction)
  {
    return;
  }

  // Set 2D outline visibility to segment referenced by action toggled
  this->setSegmentVisibility(senderAction, -1, -1, -1, visible);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setSegmentVisibility(QObject* senderObject, int visible, int visible3D, int visible2DFill, int visible2DOutline)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << " failed: segmentation node is not set";
    return;
  }

  QString segmentId = senderObject->property(ID_PROPERTY).toString();
  this->setSegmentVisibility(segmentId, visible, visible3D, visible2DFill, visible2DOutline);
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setSegmentVisibility(QString segmentId, int visible, int visible3D, int visible2DFill, int visible2DOutline)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << " failed: segmentation node is not set";
    return;
  }

  vtkMRMLSegmentationDisplayNode* displayNode = vtkMRMLSegmentationDisplayNode::SafeDownCast(
    d->SegmentationNode->GetDisplayNode() );
  if (!displayNode)
  {
    qCritical() << Q_FUNC_INFO << ": No display node for segmentation!";
    return;
  }
  vtkMRMLSegmentationDisplayNode::SegmentDisplayProperties properties;
  displayNode->GetSegmentDisplayProperties(segmentId.toStdString(), properties);

  // Change visibility to all modes
  bool valueChanged = false;
  if (visible == 0 || visible == 1)
  {
    properties.Visible = (bool)visible;

    // If overall visibility is explicitly set to true then enable all visibility options
    // to make sure that something is actually visible.
    if (properties.Visible && !properties.Visible3D && !properties.Visible2DFill && !properties.Visible2DOutline)
    {
      properties.Visible3D = true;
      properties.Visible2DFill = true;
      properties.Visible2DOutline = true;
    }

    valueChanged = true;
  }
  if (visible3D == 0 || visible3D == 1)
  {
    properties.Visible3D = (bool)visible3D;
    valueChanged = true;
  }
  if (visible2DFill == 0 || visible2DFill == 1)
  {
    properties.Visible2DFill = (bool)visible2DFill;
    valueChanged = true;
  }
  if (visible2DOutline == 0 || visible2DOutline == 1)
  {
    properties.Visible2DOutline = (bool)visible2DOutline;
    valueChanged = true;
  }

  // Set visibility to display node
  if (valueChanged)
  {
    displayNode->SetSegmentDisplayProperties(segmentId.toStdString(), properties);
  }
}

//-----------------------------------------------------------------------------
int qMRMLRtInversePlanningSegmentsTableView::segmentCount() const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);

  return d->Model->rowCount();
}

//-----------------------------------------------------------------------------
QStringList qMRMLRtInversePlanningSegmentsTableView::selectedSegmentIDs()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentsTable->selectionModel()->hasSelection())
  {
    return QStringList();
  }

  QStringList selectedSegmentIds;
  for (int row = 0; row < d->SortFilterModel->rowCount(); ++row)
  {
    if (!d->SegmentsTable->selectionModel()->isRowSelected(row, QModelIndex()))
    {
      continue;
    }
    selectedSegmentIds << d->SortFilterModel->segmentIDFromIndex(d->SortFilterModel->index(row, 0));
  }
  return selectedSegmentIds;
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setSelectedSegmentIDs(QStringList segmentIDs)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  if (!d->SegmentationNode && !segmentIDs.empty())
  {
    qCritical() << Q_FUNC_INFO << " failed: segmentation node is not set";
    return;
  }
  if (segmentIDs == this->selectedSegmentIDs())
  {
    return;
  }

  bool validSelection = false;
  MRMLNodeModifyBlocker blocker(d->SegmentationNode);
  // First segment selection should also clear other selections
  QItemSelectionModel::SelectionFlag itemSelectionFlag = QItemSelectionModel::ClearAndSelect;
  for (QString segmentID : segmentIDs)
  {
    QModelIndex index = d->SortFilterModel->indexFromSegmentID(segmentID);
    if (!index.isValid())
    {
      continue;
    }
    validSelection = true;
    QItemSelectionModel::QItemSelectionModel::SelectionFlags flags = QFlags<QItemSelectionModel::SelectionFlag>();
    flags.setFlag(itemSelectionFlag);
    flags.setFlag(QItemSelectionModel::Rows);
    d->SegmentsTable->selectionModel()->select(index, flags);
    // After the first segment, we append to the current selection
    itemSelectionFlag = QItemSelectionModel::Select;
  }

  if (!validSelection)
  {
    // The list of segment IDs was either empty, or all IDs were invalid.
    d->SegmentsTable->selectionModel()->clearSelection();
  }
}

//-----------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::clearSelection()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->clearSelection();
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::eventFilter(QObject* target, QEvent* event)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (target == d->SegmentsTable)
  {
    // Prevent giving the focus to the previous/next widget if arrow keys are used
    // at the edge of the table (without this: if the current cell is in the top
    // row and user press the Up key, the focus goes from the table to the previous
    // widget in the tab order)
    if (event->type() == QEvent::KeyPress)
    {
      QKeyEvent* keyEvent = static_cast<QKeyEvent *>(event);
      QAbstractItemModel* model = d->SegmentsTable->model();
      QModelIndex currentIndex = d->SegmentsTable->currentIndex();

      if (model && (
        (keyEvent->key() == Qt::Key_Left && currentIndex.column() == 0)
        || (keyEvent->key() == Qt::Key_Up && currentIndex.row() == 0)
        || (keyEvent->key() == Qt::Key_Right && currentIndex.column() == model->columnCount() - 1)
        || (keyEvent->key() == Qt::Key_Down && currentIndex.row() == model->rowCount() - 1)))
      {
        return true;
      }
    }
  }
  return this->QWidget::eventFilter(target, event);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::endProcessing()
{
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setSelectionMode(int mode)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->setSelectionMode(static_cast<QAbstractItemView::SelectionMode>(mode));
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setHeaderVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->horizontalHeader()->setVisible(visible);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setVisibilityColumnVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->setColumnHidden(d->Model->visibilityColumn(), !visible);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setColorColumnVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->setColumnHidden(d->Model->colorColumn(), !visible);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setOpacityColumnVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->setColumnHidden(d->Model->opacityColumn(), !visible);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setLayerColumnVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->setColumnHidden(d->Model->layerColumn(), !visible);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setReadOnly(bool aReadOnly)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (aReadOnly)
  {
    d->SegmentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  }
  else
  {
    d->SegmentsTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  }
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setFilterBarVisible(bool visible)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->FilterBar->setVisible(visible);
  d->IsFilterBarVisible = visible;
  d->SortFilterModel->setFilterEnabled(visible);
}

//------------------------------------------------------------------------------
int qMRMLRtInversePlanningSegmentsTableView::selectionMode()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return d->SegmentsTable->selectionMode();
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::headerVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return d->SegmentsTable->horizontalHeader()->isVisible();
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::visibilityColumnVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return !d->SegmentsTable->isColumnHidden(d->Model->visibilityColumn());
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::colorColumnVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return !d->SegmentsTable->isColumnHidden(d->Model->colorColumn());
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::opacityColumnVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return !d->SegmentsTable->isColumnHidden(d->Model->opacityColumn());
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::layerColumnVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return !d->SegmentsTable->isColumnHidden(d->Model->layerColumn());
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::readOnly()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return (d->SegmentsTable->editTriggers() == QAbstractItemView::NoEditTriggers);
}

//------------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::filterBarVisible()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return d->FilterBar->isVisible();
}

//------------------------------------------------------------------------------
QString qMRMLRtInversePlanningSegmentsTableView::textFilter()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  return d->SortFilterModel->textFilter();
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setTextFilter(QString filter)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SortFilterModel->setTextFilter(filter);
}

//------------------------------------------------------------------------------
int qMRMLRtInversePlanningSegmentsTableView::rowForSegmentID(QString segmentID)
{
  return this->sortFilterProxyModel()->indexFromSegmentID(segmentID).row();
}

//------------------------------------------------------------------------------
QString qMRMLRtInversePlanningSegmentsTableView::segmentIDForRow(int row)
{
  QModelIndex index = this->sortFilterProxyModel()->index(row, 0);
  return this->sortFilterProxyModel()->segmentIDFromIndex(index);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::contextMenuEvent(QContextMenuEvent* event)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  QMenu* contextMenu = new QMenu(this);

  QStringList selectedSegmentIDs = this->selectedSegmentIDs();

  if (selectedSegmentIDs.size() > 0)
  {
    QAction* showOnlySelectedAction = new QAction(tr("Show only selected segments"), this);
    QObject::connect(showOnlySelectedAction, SIGNAL(triggered()), this, SLOT(showOnlySelectedSegments()));
    contextMenu->addAction(showOnlySelectedAction);

    contextMenu->addSeparator();

    QAction* jumpSlicesAction = new QAction(tr("Jump slices"), this);
    QObject::connect(jumpSlicesAction, SIGNAL(triggered()), this, SLOT(jumpSlices()));
    contextMenu->addAction(jumpSlicesAction);

    contextMenu->addSeparator();

    QAction* moveUpAction = new QAction(tr("Move selected segments up"), this);
    QObject::connect(moveUpAction, SIGNAL(triggered()), this, SLOT(moveSelectedSegmentsUp()));
    contextMenu->addAction(moveUpAction);

    QAction* moveDownAction = new QAction(tr("Move selected segments down"), this);
    QObject::connect(moveDownAction, SIGNAL(triggered()), this, SLOT(moveSelectedSegmentsDown()));
    contextMenu->addAction(moveDownAction);
  }

  contextMenu->addSeparator();

  QAction* showFilterAction = new QAction(tr("Show filter bar"), this);
  showFilterAction->setCheckable(true);
  showFilterAction->setChecked(d->FilterBar->isVisible());
  QObject::connect(showFilterAction, SIGNAL(triggered(bool)), this, SLOT(setFilterBarVisible(bool)));
  contextMenu->addAction(showFilterAction);

  QAction* showLayerColumnAction = new QAction(tr("Show layer column"), this);
  showLayerColumnAction->setCheckable(true);
  showLayerColumnAction->setChecked(this->layerColumnVisible());
  QObject::connect(showLayerColumnAction, SIGNAL(triggered(bool)), this, SLOT(setLayerColumnVisible(bool)));
  contextMenu->addAction(showLayerColumnAction);

  QModelIndex index = d->SegmentsTable->indexAt(d->SegmentsTable->viewport()->mapFromGlobal(event->globalPos()));
  if (d->AdvancedSegmentVisibility && index.isValid())
  {
    QString segmentID = d->SortFilterModel->segmentIDFromIndex(index);

    // Get segment display properties
    vtkMRMLSegmentationDisplayNode::SegmentDisplayProperties properties;
    vtkMRMLSegmentationDisplayNode* displayNode = vtkMRMLSegmentationDisplayNode::SafeDownCast(d->SegmentationNode->GetDisplayNode());
    if (displayNode)
    {
      displayNode->GetSegmentDisplayProperties(segmentID.toUtf8().constData(), properties);
    }

    contextMenu->addSeparator();

    QAction* visibility3DAction = new QAction(tr("Show in 3D"), this);
    visibility3DAction->setCheckable(true);
    visibility3DAction->setChecked(properties.Visible3D);
    visibility3DAction->setProperty(ID_PROPERTY, segmentID);
    QObject::connect(visibility3DAction, SIGNAL(triggered(bool)), this, SLOT(onVisibility3DActionToggled(bool)));
    contextMenu->addAction(visibility3DAction);

    QAction* visibility2DFillAction = new QAction(tr("Show in 2D as fill"), this);
    visibility2DFillAction->setCheckable(true);
    visibility2DFillAction->setChecked(properties.Visible2DFill);
    visibility2DFillAction->setProperty(ID_PROPERTY, segmentID);
    connect(visibility2DFillAction, SIGNAL(triggered(bool)), this, SLOT(onVisibility2DFillActionToggled(bool)));
    contextMenu->addAction(visibility2DFillAction);

    QAction* visibility2DOutlineAction = new QAction(tr("Show in 2D as outline"), this);
    visibility2DOutlineAction->setCheckable(true);
    visibility2DOutlineAction->setChecked(properties.Visible2DOutline);
    visibility2DOutlineAction->setProperty(ID_PROPERTY, segmentID);
    connect(visibility2DOutlineAction, SIGNAL(triggered(bool)), this, SLOT(onVisibility2DOutlineActionToggled(bool)));
    contextMenu->addAction(visibility2DOutlineAction);
  }

    contextMenu->addSeparator();
    QAction* clearSelectedSegmentAction = new QAction(tr("Clear selected segments"), this);
    QObject::connect(clearSelectedSegmentAction, SIGNAL(triggered()), this, SLOT(clearSelectedSegments()));
    contextMenu->addAction(clearSelectedSegmentAction);
  }

  contextMenu->popup(event->globalPos());
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::clearSelectedSegments()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);

  QAction* clearSegmentAction = qobject_cast<QAction*>(sender());
  Q_ASSERT(clearSegmentAction);

  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << "Invalid segmentation node";
    return;
  }
  vtkSegmentation* segmentation = d->SegmentationNode->GetSegmentation();
  if (!segmentation)
  {
    qCritical() << Q_FUNC_INFO << "Invalid segmentation";
    return;
  }

  QMessageBox messageBox;
  messageBox.addButton(tr("Clear"), QMessageBox::ButtonRole::AcceptRole);
  QPushButton* cancelButton = messageBox.addButton(tr("Cancel"), QMessageBox::ButtonRole::RejectRole);
  messageBox.setDefaultButton(cancelButton);
  messageBox.setText(tr("Are you sure you want to clear the contents of the selected segments?"));
  if (messageBox.exec() == QMessageBox::ButtonRole::RejectRole)
  {
    return;
  }

  QStringList selectedSegmentIDs = this->selectedSegmentIDs();
  for (QString segmentID : selectedSegmentIDs)
  {
    vtkSlicerSegmentationsModuleLogic::ClearSegment(d->SegmentationNode, segmentID.toStdString());
  }
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::showOnlySelectedSegments()
{
  QStringList selectedSegmentIDs = this->selectedSegmentIDs();
  if (selectedSegmentIDs.size() == 0)
  {
    qWarning() << Q_FUNC_INFO << ": No segment selected";
    return;
  }

  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << ": No current segmentation node";
    return;
  }
  vtkMRMLSegmentationDisplayNode* displayNode = vtkMRMLSegmentationDisplayNode::SafeDownCast(
    d->SegmentationNode->GetDisplayNode() );
  if (!displayNode)
  {
    qCritical() << Q_FUNC_INFO << ": No display node for segmentation " << d->SegmentationNode->GetName();
    return;
  }

  vtkSegmentation* segmentation = d->SegmentationNode->GetSegmentation();
  if (!segmentation)
  {
    qCritical() << Q_FUNC_INFO << ": No segmentation";
    return;
  }

  std::vector<std::string> displayedSegmentIDs;
  segmentation->GetSegmentIDs(displayedSegmentIDs);

  QStringList hiddenSegmentIDs = d->SortFilterModel->hideSegments();

  // Hide all segments except the selected ones
  MRMLNodeModifyBlocker blocker(displayNode);
  for (std::string displayedID : displayedSegmentIDs)
  {
    QString segmentID = QString::fromStdString(displayedID);
    if (hiddenSegmentIDs.contains(segmentID))
    {
      continue;
    }

    bool visible = false;
    if (selectedSegmentIDs.contains(segmentID))
    {
      visible = true;
    }

    displayNode->SetSegmentVisibility(displayedID, visible);
  }
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::jumpSlices()
{
  QStringList selectedSegmentIDs = this->selectedSegmentIDs();
  if (selectedSegmentIDs.size() == 0)
  {
    // No segment selected
    return;
  }

  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    // No current segmentation node
    return;
  }

  double* segmentCenterPosition = d->SegmentationNode->GetSegmentCenterRAS(selectedSegmentIDs[0].toUtf8().constData());
  if (!segmentCenterPosition)
  {
    return;
  }

  qSlicerLayoutManager* layoutManager = qSlicerApplication::application()->layoutManager();
  if (!layoutManager)
  {
    // application is closing
    return;
  }
  foreach(QString sliceViewName, layoutManager->sliceViewNames())
  {
    // Check if segmentation is visible in this view
    qMRMLSliceWidget* sliceWidget = layoutManager->sliceWidget(sliceViewName);
    vtkMRMLSliceNode* sliceNode = sliceWidget->mrmlSliceNode();
    if (!sliceNode || !sliceNode->GetID())
    {
      continue;
    }
    bool visibleInView = false;
    int numberOfDisplayNodes = d->SegmentationNode->GetNumberOfDisplayNodes();
    for (int displayNodeIndex = 0; displayNodeIndex < numberOfDisplayNodes; displayNodeIndex++)
    {
      vtkMRMLDisplayNode* segmentationDisplayNode = d->SegmentationNode->GetNthDisplayNode(displayNodeIndex);
      if (segmentationDisplayNode && segmentationDisplayNode->IsDisplayableInView(sliceNode->GetID()))
      {
        visibleInView = true;
        break;
      }
    }
    if (!visibleInView)
    {
      continue;
    }
    sliceNode->JumpSliceByCentering(segmentCenterPosition[0], segmentCenterPosition[1], segmentCenterPosition[2]);
    // snap to IJK to make sure slice is not positioned at the boundary of two voxels
    vtkSlicerApplicationLogic* appLogic = qSlicerApplication::application()->applicationLogic();
    if (appLogic)
    {
      vtkMRMLSliceLogic* sliceLogic = appLogic->GetSliceLogic(sliceNode);
      if (sliceLogic)
      {
        sliceLogic->SnapSliceOffsetToIJK();
      }
    }
  }
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::moveSelectedSegmentsUp()
{
  QStringList selectedSegmentIDs = this->selectedSegmentIDs();
  if (selectedSegmentIDs.size() == 0)
  {
    qWarning() << Q_FUNC_INFO << ": No segment selected";
    return;
  }

  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << ": No current segmentation node";
    return;
  }
  vtkSegmentation* segmentation = d->SegmentationNode->GetSegmentation();

  QModelIndexList segmentModelIndices;
  QList<int> selectedRows;
  foreach (QString segmentID, selectedSegmentIDs)
  {
    QModelIndex index = d->SortFilterModel->indexFromSegmentID(segmentID);
    segmentModelIndices << index;
    selectedRows << index.row();
  }
  int minIndex = *(std::min_element(selectedRows.begin(), selectedRows.end()));
  if (minIndex == 0)
  {
    qDebug() << Q_FUNC_INFO << ": Cannot move top segment up";
    return;
  }

  for (int i = 0; i < selectedSegmentIDs.size(); ++i)
  {
    QModelIndex selectedModelIndex = segmentModelIndices[i];
    QModelIndex previousModelIndex = d->SortFilterModel->index(selectedModelIndex.row() - 1, 0);
    QString previousSegmentID = d->SortFilterModel->segmentIDFromIndex(previousModelIndex);
    int previousSegmentIndex = segmentation->GetSegmentIndex(previousSegmentID.toStdString());
    segmentation->SetSegmentIndex(selectedSegmentIDs[i].toUtf8().constData(), previousSegmentIndex);
  }
  this->setSelectedSegmentIDs(selectedSegmentIDs);
}

//------------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::moveSelectedSegmentsDown()
{
  QStringList selectedSegmentIDs = this->selectedSegmentIDs();
  if (selectedSegmentIDs.size() == 0)
  {
    qWarning() << Q_FUNC_INFO << ": No segment selected";
    return;
  }

  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  if (!d->SegmentationNode)
  {
    qCritical() << Q_FUNC_INFO << ": No current segmentation node";
    return;
  }
  vtkSegmentation* segmentation = d->SegmentationNode->GetSegmentation();

  QModelIndexList segmentModelIndices;
  QList<int> selectedRows;
  foreach(QString segmentID, selectedSegmentIDs)
  {
    QModelIndex index = d->SortFilterModel->indexFromSegmentID(segmentID);
    segmentModelIndices << index;
    selectedRows << index.row();
  }
  int maxIndex = *(std::max_element(selectedRows.begin(), selectedRows.end()));
  if (maxIndex == d->SortFilterModel->rowCount() - 1)
  {
    qDebug() << Q_FUNC_INFO << ": Cannot move bottom segment down";
    return;
  }

  for (int i = selectedSegmentIDs.count() - 1; i >= 0; --i)
  {
    QModelIndex selectedModelIndex = segmentModelIndices[i];
    QModelIndex nextModelIndex = d->SortFilterModel->index(selectedModelIndex.row() + 1, 0);
    QString nextSegmentID = d->SortFilterModel->segmentIDFromIndex(nextModelIndex);
    int nextSegmentIndex = segmentation->GetSegmentIndex(nextSegmentID.toStdString());
    segmentation->SetSegmentIndex(selectedSegmentIDs[i].toUtf8().constData(), nextSegmentIndex);
  }
  this->setSelectedSegmentIDs(selectedSegmentIDs);
}

// --------------------------------------------------------------------------
QString qMRMLRtInversePlanningSegmentsTableView::terminologyTooltipForSegment(vtkSegment* segment)
{
  return qMRMLSegmentsModel::terminologyTooltipForSegment(segment);
}

// --------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setHideSegments(const QStringList& segmentIDs)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SortFilterModel->setHideSegments(segmentIDs);
}

// --------------------------------------------------------------------------
QStringList qMRMLRtInversePlanningSegmentsTableView::hideSegments()const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);
  return d->SortFilterModel->hideSegments();
}

// --------------------------------------------------------------------------
QStringList qMRMLRtInversePlanningSegmentsTableView::displayedSegmentIDs()const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);

  QStringList displayedSegmentIDs;
  for (int row = 0; row < d->SortFilterModel->rowCount(); ++row)
  {
    displayedSegmentIDs << d->SortFilterModel->segmentIDFromIndex(d->SortFilterModel->index(row, 0));
  }
  return displayedSegmentIDs;
}

// --------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::setJumpToSelectedSegmentEnabled(bool enable)
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->JumpToSelectedSegmentEnabled = enable;
}

// --------------------------------------------------------------------------
bool qMRMLRtInversePlanningSegmentsTableView::jumpToSelectedSegmentEnabled()const
{
  Q_D(const qMRMLRtInversePlanningSegmentsTableView);
  return d->JumpToSelectedSegmentEnabled;
}

// --------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::modelAboutToBeReset()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->WasBlockingTableSignalsBeforeReset = d->SegmentsTable->blockSignals(true);
  d->SelectedSegmentIDsBeforeReset = this->selectedSegmentIDs();
}

// --------------------------------------------------------------------------
void qMRMLRtInversePlanningSegmentsTableView::modelReset()
{
  Q_D(qMRMLRtInversePlanningSegmentsTableView);
  d->SegmentsTable->blockSignals(d->WasBlockingTableSignalsBeforeReset);
  this->setSelectedSegmentIDs(d->SelectedSegmentIDsBeforeReset);
  d->SelectedSegmentIDsBeforeReset.clear();
}
