/*==============================================================================

Program: 3D Slicer

Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

==============================================================================*/

// DicomRtImport includes
#include "vtkSlicerDicomRtImportModuleLogic.h"
#include "vtkSlicerDicomRtReader.h"
#include "vtkDICOMImportInfo.h"
#include "vtkTopologicalHierarchy.h"

// SlicerRT includes
#include "SlicerRtCommon.h"
#include "vtkMRMLContourNode.h"
#include "vtkMRMLContourHierarchyNode.h"

// DCMTK includes
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/ofstd/ofcond.h>
#include <dcmtk/ofstd/ofstring.h>
#include <dcmtk/ofstd/ofstd.h>        /* for class OFStandard */

// MRML includes
#include <vtkMRMLModelDisplayNode.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLModelHierarchyNode.h>
#include <vtkMRMLAnnotationHierarchyNode.h>
#include <vtkMRMLAnnotationPointDisplayNode.h>
#include <vtkMRMLAnnotationFiducialNode.h>
#include <vtkMRMLAnnotationTextDisplayNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLScalarVolumeDisplayNode.h>
#include <vtkMRMLVolumeArchetypeStorageNode.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLColorTableNode.h>

// VTK includes
#include <vtkPolyData.h>
#include <vtkImageData.h>
#include <vtkSmartPointer.h>
#include <vtkImageCast.h>
#include <vtkStringArray.h>
#include <vtkLookupTable.h>

// STD includes
#include <cassert>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerDicomRtImportModuleLogic);
vtkCxxSetObjectMacro(vtkSlicerDicomRtImportModuleLogic, VolumesLogic, vtkSlicerVolumesLogic);

//----------------------------------------------------------------------------
vtkSlicerDicomRtImportModuleLogic::vtkSlicerDicomRtImportModuleLogic()
{
  this->VolumesLogic = NULL;

  this->AutoContourOpacityOn();
}

//----------------------------------------------------------------------------
vtkSlicerDicomRtImportModuleLogic::~vtkSlicerDicomRtImportModuleLogic()
{
  this->SetVolumesLogic(NULL); // release the volumes logic object
}

//----------------------------------------------------------------------------
void vtkSlicerDicomRtImportModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
void vtkSlicerDicomRtImportModuleLogic::RegisterNodes()
{
  assert(this->GetMRMLScene() != 0);

  this->GetMRMLScene()->RegisterNodeClass(vtkSmartPointer<vtkMRMLContourNode>::New());
  this->GetMRMLScene()->RegisterNodeClass(vtkSmartPointer<vtkMRMLContourHierarchyNode>::New());
}

//---------------------------------------------------------------------------
void vtkSlicerDicomRtImportModuleLogic::Examine(vtkDICOMImportInfo *importInfo)
{
  importInfo->RemoveAllLoadables();

  for (int fileListIndex=0; fileListIndex<importInfo->GetNumberOfFileLists(); fileListIndex++)
  {
    vtkStringArray *fileList=importInfo->GetFileList(fileListIndex);
    for (int fileIndex=0; fileIndex<fileList->GetNumberOfValues(); fileIndex++)
    {
      DcmFileFormat fileformat;

      vtkStdString fileName=fileList->GetValue(fileIndex);
      OFCondition result;
      result = fileformat.loadFile(fileName.c_str(), EXS_Unknown);
      if (!result.good())
      {
        continue; // failed to parse this file, skip it
      }
      DcmDataset *dataset = fileformat.getDataset();
      // check SOP Class UID for one of the supported RT objects
      OFString sopClass;
      if (!dataset->findAndGetOFString(DCM_SOPClassUID, sopClass).good() || sopClass.empty())
      {
        continue; // failed to parse this file, skip it
      }    
      
      // DICOM parsing is successful, now check if the object is loadable 
      std::string name;
      std::string tooltip;
      std::string warning;
      bool selected=true;
      double confidence=0.9; // almost sure, it's not 1.0 to allow user modules to override this importer

      OFString seriesNumber;
      dataset->findAndGetOFString(DCM_SeriesNumber, seriesNumber);
      if (!seriesNumber.empty())
      {
        name+=std::string(seriesNumber.c_str())+": ";
      }

      if (sopClass == UID_RTDoseStorage)
      {
        name+="RTDOSE";
        OFString instanceNumber;
        dataset->findAndGetOFString(DCM_InstanceNumber, instanceNumber);
        OFString seriesDescription;
        dataset->findAndGetOFString(DCM_SeriesDescription, seriesDescription);
        if (!seriesDescription.empty())
        {
          name+=std::string(": ")+seriesDescription.c_str(); 
        }
        if (!instanceNumber.empty())
        {
          name+=std::string(" [")+instanceNumber.c_str()+"]"; 
        }
      }
      else if (sopClass == UID_RTPlanStorage)
      {
        name+="RTPLAN";
        OFString planLabel;
        dataset->findAndGetOFString(DCM_RTPlanLabel, planLabel);
        OFString planName;
        dataset->findAndGetOFString(DCM_RTPlanName, planName);
        if (!planLabel.empty() && !planName.empty())
        {
          if (planLabel.compare(planName)!=0)
          {
            // plan label and name is different, display both
            name+=std::string(": ")+planLabel.c_str()+" ("+planName.c_str()+")";
          }
          else
          {
            name+=std::string(": ")+planLabel.c_str();
          }
        }
        else if (!planLabel.empty() && planName.empty())
        {
          name+=std::string(": ")+planLabel.c_str();
        }
        else if (planLabel.empty() && !planName.empty())
        {
          name+=std::string(": ")+planName.c_str();
        }
      }
      else if (sopClass == UID_RTStructureSetStorage)
      {
        name+="RTSTRUCT";
        OFString structLabel;
        dataset->findAndGetOFString(DCM_StructureSetLabel, structLabel);
        if (!structLabel.empty())
        {
          name+=std::string(": ")+structLabel.c_str();
        }
      }
      /* not yet supported
      else if (sopClass == UID_RTImageStorage)
      else if (sopClass == UID_RTTreatmentSummaryRecordStorage)
      else if (sopClass == UID_RTIonPlanStorage)
      else if (sopClass == UID_RTIonBeamsTreatmentRecordStorage)
      */
      else
      {
        continue; // not an RT file
      }

      // The object is stored in a single file
      vtkSmartPointer<vtkStringArray> loadableFileList=vtkSmartPointer<vtkStringArray>::New();
      loadableFileList->InsertNextValue(fileName);
     
      importInfo->InsertNextLoadable(loadableFileList, name.c_str(), tooltip.c_str(), warning.c_str(), selected, confidence);
    }
  }
}

//---------------------------------------------------------------------------
bool vtkSlicerDicomRtImportModuleLogic::LoadDicomRT(vtkDICOMImportInfo *loadInfo)
{
  bool loadSuccessful = false;
  if (!loadInfo || !loadInfo->GetLoadableFiles(0) || loadInfo->GetLoadableFiles(0)->GetNumberOfValues() < 1)
  {
    vtkErrorMacro("Unable to load Dicom RT data due to invalid loadable information.");
    return loadSuccessful;
  }

  vtkStdString firstFileNameStr = loadInfo->GetLoadableFiles(0)->GetValue(0);
  const char* seriesName = loadInfo->GetLoadableName(0);
  std::cout << "Loading series '" << seriesName << "' from file '" << firstFileNameStr << "'" << std::endl;

  vtkSmartPointer<vtkSlicerDicomRtReader> rtReader = vtkSmartPointer<vtkSlicerDicomRtReader>::New();
  rtReader->SetFileName(firstFileNameStr.c_str());
  rtReader->Update();

  // One series can contain composite information, e.g, an RTPLAN series can contain structure sets and plans as well

  bool loadingErrorsOccurred=false;

  // Hierarchy node for the loaded structure sets
  // It is not created here yet because maybe there won't be anything to put in it.
  vtkSmartPointer<vtkMRMLModelHierarchyNode> modelHierarchyRootNode;
  vtkSmartPointer<vtkMRMLContourHierarchyNode> contourHierarchyRootNode;    
  vtkSmartPointer<vtkMRMLAnnotationHierarchyNode> isocenterHierarchyRootNode;    

  // RTSTRUCT
  if (rtReader->GetLoadRTStructureSetSuccessful())
  {
    this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState); 

    // Add color table node
    vtkSmartPointer<vtkMRMLColorTableNode> structureSetColorTableNode = vtkSmartPointer<vtkMRMLColorTableNode>::New();
    std::string structureSetColorTableNodeName;
    structureSetColorTableNodeName = std::string(seriesName) + SlicerRtCommon::DICOMRTIMPORT_COLOR_TABLE_NODE_NAME_POSTFIX;
    structureSetColorTableNodeName = this->GetMRMLScene()->GenerateUniqueName(structureSetColorTableNodeName);
    structureSetColorTableNode->SetName(structureSetColorTableNodeName.c_str());
    structureSetColorTableNode->HideFromEditorsOff();
    structureSetColorTableNode->SetTypeToUser();

    this->GetMRMLScene()->AddNode(structureSetColorTableNode);

    // Add ROIs
    int numberOfRois = rtReader->GetNumberOfRois();
    structureSetColorTableNode->SetNumberOfColors(numberOfRois+2);
    structureSetColorTableNode->GetLookupTable()->SetTableRange(0,numberOfRois+1);
    structureSetColorTableNode->AddColor("Background", 0.0, 0.0, 0.0, 0.0); // Black background
    structureSetColorTableNode->AddColor("Invalid", 0.5, 0.5, 0.5, 1.0); // Color indicating invalid index

    vtkSmartPointer<vtkPolyDataCollection> roiCollection = vtkSmartPointer<vtkPolyDataCollection>::New();
    vtkSmartPointer<vtkCollection> displayNodeCollection = vtkSmartPointer<vtkCollection>::New();

    for (int internalROIIndex=0; internalROIIndex<numberOfRois; internalROIIndex++) // DICOM starts indexing from 1
    {
      const char* roiLabel = rtReader->GetRoiName(internalROIIndex);
      double *roiColor = rtReader->GetRoiDisplayColor(internalROIIndex);
      vtkMRMLDisplayableNode* addedDisplayableNode = NULL;

      // Save color into the color table
      structureSetColorTableNode->AddColor(roiLabel, roiColor[0], roiColor[1], roiColor[2]);

      // Get structure
      vtkPolyData* roiPoly = rtReader->GetRoiPolyData(internalROIIndex);
      if (roiPoly == NULL)
      {
        vtkWarningMacro("Cannot read polydata from file: " << firstFileNameStr << ", ROI: "<<internalROIIndex);
        continue;
      }
      if (roiPoly->GetNumberOfPoints() < 1)
      {
        vtkWarningMacro("The ROI polydata does not contain any points, file: " << firstFileNameStr << ", ROI: "<<internalROIIndex);
        continue;
      }

      std::string contourNodeName;
      contourNodeName = std::string(roiLabel) + SlicerRtCommon::DICOMRTIMPORT_CONTOUR_NODE_NAME_POSTFIX;
      contourNodeName = this->GetMRMLScene()->GenerateUniqueName(contourNodeName);

      if (roiPoly->GetNumberOfPoints() == 1)
      {	
        // Point ROI
        addedDisplayableNode = AddRoiPoint(roiPoly->GetPoint(0), contourNodeName, roiColor);
      }
      else
      {
        // Contour ROI
        addedDisplayableNode = AddRoiContour(roiPoly, contourNodeName, roiColor);

        roiCollection->AddItem(roiPoly);

        // Create Contour node
        if (addedDisplayableNode)
        {
          // Create root contour hierarchy node, if it has not been created yet
          if (contourHierarchyRootNode.GetPointer()==NULL)
          {
            contourHierarchyRootNode = vtkSmartPointer<vtkMRMLContourHierarchyNode>::New();
            std::string hierarchyNodeName;
            hierarchyNodeName = std::string(seriesName) + SlicerRtCommon::DICOMRTIMPORT_ROOT_CONTOUR_HIERARCHY_NODE_NAME_POSTFIX;
            hierarchyNodeName = this->GetMRMLScene()->GenerateUniqueName(hierarchyNodeName);
            contourHierarchyRootNode->SetName(hierarchyNodeName.c_str());
            contourHierarchyRootNode->AllowMultipleChildrenOn();
            contourHierarchyRootNode->HideFromEditorsOff();
            contourHierarchyRootNode->SetAttribute(SlicerRtCommon::DICOMRTIMPORT_SERIES_NAME_ATTRIBUTE_NAME.c_str(), seriesName);
            this->GetMRMLScene()->AddNode(contourHierarchyRootNode);
          }

          vtkSmartPointer<vtkMRMLContourNode> contourNode = vtkSmartPointer<vtkMRMLContourNode>::New();
          contourNode = vtkMRMLContourNode::SafeDownCast(this->GetMRMLScene()->AddNode(contourNode));
          contourNode->SetName(contourNodeName.c_str());
          contourNode->SetStructureName(roiLabel);
          contourNode->SetAndObserveRibbonModelNodeId(addedDisplayableNode->GetID());
          contourNode->SetActiveRepresentationByNode(addedDisplayableNode);
          contourNode->HideFromEditorsOff();

          // Put the contour node in the hierarchy
          vtkSmartPointer<vtkMRMLContourHierarchyNode> contourHierarchyNode = vtkSmartPointer<vtkMRMLContourHierarchyNode>::New();
          this->GetMRMLScene()->AddNode(contourHierarchyNode);
          contourHierarchyNode->SetParentNodeID( contourHierarchyRootNode->GetID() );
          contourHierarchyNode->SetDisplayableNodeID( contourNode->GetID() );

          displayNodeCollection->AddItem( vtkMRMLModelNode::SafeDownCast(addedDisplayableNode)->GetModelDisplayNode() );
        }
      }

      // Add new node to the hierarchy node
      if (addedDisplayableNode)
      {
        // Create root model hierarchy node, if it has not been created yet
        if (modelHierarchyRootNode.GetPointer()==NULL)
        {
          modelHierarchyRootNode = vtkSmartPointer<vtkMRMLModelHierarchyNode>::New();
          std::string hierarchyNodeName;
          hierarchyNodeName = std::string(seriesName) + SlicerRtCommon::DICOMRTIMPORT_ROOT_MODEL_HIERARCHY_NODE_NAME_POSTFIX;
          hierarchyNodeName = this->GetMRMLScene()->GenerateUniqueName(hierarchyNodeName);
          modelHierarchyRootNode->SetName(hierarchyNodeName.c_str());
          modelHierarchyRootNode->AllowMultipleChildrenOn();
          modelHierarchyRootNode->HideFromEditorsOff();
          this->GetMRMLScene()->AddNode(modelHierarchyRootNode);

          // A hierarchy node needs a display node
          vtkSmartPointer<vtkMRMLModelDisplayNode> modelDisplayNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
          hierarchyNodeName.append("Display");
          modelDisplayNode->SetName(hierarchyNodeName.c_str());
          modelDisplayNode->SetVisibility(1);
          this->GetMRMLScene()->AddNode(modelDisplayNode);
          modelHierarchyRootNode->SetAndObserveDisplayNodeID( modelDisplayNode->GetID() );
        }

        // Put the new node in the hierarchy
        vtkSmartPointer<vtkMRMLModelHierarchyNode> modelHierarchyNode = vtkSmartPointer<vtkMRMLModelHierarchyNode>::New();
        this->GetMRMLScene()->AddNode(modelHierarchyNode);
        modelHierarchyNode->SetParentNodeID( modelHierarchyRootNode->GetID() );
        modelHierarchyNode->SetModelNodeID( addedDisplayableNode->GetID() );
      }
    }

    // Set opacities according to topological hierarchy levels
    if (this->AutoContourOpacity)
    {
      if (roiCollection->GetNumberOfItems() == displayNodeCollection->GetNumberOfItems())
      {
        vtkSmartPointer<vtkTopologicalHierarchy> topologicalHierarchy = vtkSmartPointer<vtkTopologicalHierarchy>::New();
        topologicalHierarchy->SetInputPolyDataCollection(roiCollection);
        topologicalHierarchy->Update();
        vtkIntArray* levels = topologicalHierarchy->GetOutputLevels();

        int numberOfLevels = 0;
        for (int i=0; i<levels->GetNumberOfTuples(); ++i)
        {
          if (levels->GetValue(i) > numberOfLevels)
          {
            numberOfLevels = levels->GetValue(i);
          }
        }

        for (int i=0; i<roiCollection->GetNumberOfItems(); ++i)
        {
          int level = levels->GetValue(i);
          vtkMRMLModelDisplayNode* displayNode = vtkMRMLModelDisplayNode::SafeDownCast(
            displayNodeCollection->GetItemAsObject(i) );
          if (displayNode)
          {
            // The opacity level is set evenly distributed between 0 and 1 (excluding 0)
            // according to the topological hierarchy level of the contour
            displayNode->SetOpacity( 1.0 - ((double)level) / (numberOfLevels+1) );
          }
        }
      }
      else
      {
        vtkWarningMacro("Unable to auto-determine opacity: Number of ROIs and display nodes do not match!");
      }
    }

    this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState); 
    loadSuccessful=true;
  }

  // RTDOSE
  if (rtReader->GetLoadRTDoseSuccessful())
  {
    // Load Volume
    vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode> volumeStorageNode = vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode>::New();
    vtkSmartPointer<vtkMRMLScalarVolumeNode> volumeNode = vtkSmartPointer<vtkMRMLScalarVolumeNode>::New();
    volumeStorageNode->SetFileName(firstFileNameStr.c_str());
    volumeStorageNode->ResetFileNameList();
    for (int fileIndex=0; fileIndex<loadInfo->GetLoadableFiles(0)->GetNumberOfValues(); ++fileIndex)
    {
      volumeStorageNode->AddFileName(loadInfo->GetLoadableFiles(0)->GetValue(fileIndex).c_str());
    }
    volumeStorageNode->SetSingleFile(0);

    if (volumeStorageNode->ReadData(volumeNode))
    {
      volumeNode->SetScene(this->GetMRMLScene());
      std::string volumeNodeName = this->GetMRMLScene()->GenerateUniqueName(seriesName);
      volumeNode->SetName(volumeNodeName.c_str());
      this->GetMRMLScene()->AddNode(volumeNode);

      // Set new spacing
      double* initialSpacing = volumeNode->GetSpacing();
      double* correctSpacing = rtReader->GetPixelSpacing();
      volumeNode->SetSpacing(correctSpacing[0], correctSpacing[1], initialSpacing[2]);
      volumeNode->SetAttribute(SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME.c_str(), rtReader->GetDoseUnits());
      volumeNode->SetAttribute(SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_VALUE_ATTRIBUTE_NAME.c_str(), rtReader->GetDoseGridScaling());

      // Apply dose grid scaling
      vtkSmartPointer<vtkImageData> floatVolumeData = vtkSmartPointer<vtkImageData>::New();

      vtkSmartPointer<vtkImageCast> imageCast = vtkSmartPointer<vtkImageCast>::New();
      imageCast->SetInput(volumeNode->GetImageData());
      imageCast->SetOutputScalarTypeToFloat();
      imageCast->Update();
      floatVolumeData->DeepCopy(imageCast->GetOutput());

      std::stringstream ss;
      ss << rtReader->GetDoseGridScaling();
      double doubleValue;
      ss >> doubleValue;
      double doseGridScaling = doubleValue;

      float value = 0.0;
      float* floatPtr = (float*)floatVolumeData->GetScalarPointer();
      for (long i=0; i<floatVolumeData->GetNumberOfPoints(); ++i)
      {
        value = (*floatPtr) * doseGridScaling;
        (*floatPtr) = value;
        ++floatPtr;
      }

      volumeNode->SetAndObserveImageData(floatVolumeData);      

      // Set default colormap to rainbow
      vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode> volumeDisplayNode = vtkSmartPointer<vtkMRMLScalarVolumeDisplayNode>::New();
      volumeDisplayNode->SetAndObserveColorNodeID("vtkMRMLColorTableNodeRainbow");
      this->GetMRMLScene()->AddNode(volumeDisplayNode);
      volumeNode->SetAndObserveDisplayNodeID(volumeDisplayNode->GetID());

      // Select as active volume
      if (this->GetApplicationLogic()!=NULL)
      {
        if (this->GetApplicationLogic()->GetSelectionNode()!=NULL)
        {
          this->GetApplicationLogic()->GetSelectionNode()->SetReferenceActiveVolumeID(volumeNode->GetID());
          this->GetApplicationLogic()->PropagateVolumeSelection();
        }
      }
      loadSuccessful=true;
    }
    else
    {
      vtkErrorMacro("Failed to load dose volume file '" << firstFileNameStr << "' (series name '" << seriesName << "')");
      loadingErrorsOccurred=true;
    }
  }

  // RTPLAN
  if (rtReader->GetLoadRTPlanSuccessful())
  {
    this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState); 

    vtkMRMLDisplayableNode* addedDisplayableNode = NULL;
    int numberOfBeams = rtReader->GetNumberOfBeams();
    for (int dicomBeamIndex = 1; dicomBeamIndex < numberOfBeams+1; dicomBeamIndex++) // DICOM starts indexing from 1
    {
      // Isocenter fiducial
      double isoColor[3] = { 1.0, 1.0, 1.0 };
      addedDisplayableNode= this->AddRoiPoint(rtReader->GetBeamIsocenterPositionRas(dicomBeamIndex), rtReader->GetBeamName(dicomBeamIndex), isoColor);

      // Add new node to the hierarchy node
      if (addedDisplayableNode)
      {
        // Create root node, if it has not been created yet
        if (isocenterHierarchyRootNode.GetPointer()==NULL)
        {
          isocenterHierarchyRootNode = vtkSmartPointer<vtkMRMLAnnotationHierarchyNode>::New();
          std::string hierarchyNodeName;
          hierarchyNodeName = std::string(seriesName) + SlicerRtCommon::DICOMRTIMPORT_ISOCENTER_HIERARCHY_NODE_NAME_POSTFIX;
          hierarchyNodeName = this->GetMRMLScene()->GenerateUniqueName(hierarchyNodeName);
          isocenterHierarchyRootNode->SetName(hierarchyNodeName.c_str());
          isocenterHierarchyRootNode->AllowMultipleChildrenOn();
          isocenterHierarchyRootNode->HideFromEditorsOff();
          this->GetMRMLScene()->AddNode(isocenterHierarchyRootNode);

          // A hierarchy node needs a display node
          vtkSmartPointer<vtkMRMLModelDisplayNode> modelDisplayNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
          hierarchyNodeName.append("Display");
          modelDisplayNode->SetName(hierarchyNodeName.c_str());
          modelDisplayNode->SetVisibility(1);
          this->GetMRMLScene()->AddNode(modelDisplayNode);
          isocenterHierarchyRootNode->SetAndObserveDisplayNodeID( modelDisplayNode->GetID() );

          // Add attributes containing beam information to the isocenter fiducial node
          // TODO: Add these in the PatientHierarchy node when available
          std::stringstream sourceAxisDistanceStream;
          sourceAxisDistanceStream << rtReader->GetBeamSourceAxisDistance(dicomBeamIndex);
          addedDisplayableNode->SetAttribute( SlicerRtCommon::DICOMRTIMPORT_BEAM_SOURCE_AXIS_DISTANCE_ATTRIBUTE_NAME.c_str(),
            sourceAxisDistanceStream.str().c_str() );
          std::stringstream gantryAngleStream;
          gantryAngleStream << rtReader->GetBeamGantryAngle(dicomBeamIndex);
          addedDisplayableNode->SetAttribute( SlicerRtCommon::DICOMRTIMPORT_BEAM_GANTRY_ANGLE_ATTRIBUTE_NAME.c_str(),
            gantryAngleStream.str().c_str() );
          std::stringstream couchAngleStream;
          couchAngleStream << rtReader->GetBeamPatientSupportAngle(dicomBeamIndex);
          addedDisplayableNode->SetAttribute( SlicerRtCommon::DICOMRTIMPORT_BEAM_COUCH_ANGLE_ATTRIBUTE_NAME.c_str(),
            couchAngleStream.str().c_str() );
          std::stringstream collimatorAngleStream;
          collimatorAngleStream << rtReader->GetBeamBeamLimitingDeviceAngle(dicomBeamIndex);
          addedDisplayableNode->SetAttribute( SlicerRtCommon::DICOMRTIMPORT_BEAM_COLLIMATOR_ANGLE_ATTRIBUTE_NAME.c_str(),
            collimatorAngleStream.str().c_str() );
          std::stringstream jawPositionsStream;
          double jawPositions[2][2];
          rtReader->GetBeamLeafJawPositions(dicomBeamIndex, jawPositions);
          jawPositionsStream << jawPositions[0][0] << "," << jawPositions[0][1] << ","
            << jawPositions[1][0] << "," << jawPositions[1][1];
          addedDisplayableNode->SetAttribute( SlicerRtCommon::DICOMRTIMPORT_BEAM_JAW_POSITIONS_ATTRIBUTE_NAME.c_str(),
            jawPositionsStream.str().c_str() );
        }

        // put the new node in the hierarchy
        vtkSmartPointer<vtkMRMLAnnotationHierarchyNode> isocenterHierarchyNode = vtkSmartPointer<vtkMRMLAnnotationHierarchyNode>::New();
        this->GetMRMLScene()->AddNode(isocenterHierarchyNode);
        isocenterHierarchyNode->SetParentNodeID( isocenterHierarchyRootNode->GetID() );
        isocenterHierarchyNode->SetDisplayableNodeID( addedDisplayableNode->GetID() );
      }
    }

    this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState); 
    loadSuccessful=true;
  }

  if (loadingErrorsOccurred)
  {
    return false;
  }
  return loadSuccessful;
}

//---------------------------------------------------------------------------
vtkMRMLDisplayableNode* vtkSlicerDicomRtImportModuleLogic::AddRoiPoint(double *roiPosition, std::string baseName, double *roiColor)
{
  vtkSmartPointer<vtkMRMLAnnotationFiducialNode> fiducialNode = vtkSmartPointer<vtkMRMLAnnotationFiducialNode>::New();
  std::string fiducialNodeName = this->GetMRMLScene()->GenerateUniqueName(baseName);
  fiducialNode->SetName(baseName.c_str());
  fiducialNode->AddControlPoint(roiPosition, 0, 1);
  fiducialNode->SetLocked(1);
  this->GetMRMLScene()->AddNode(fiducialNode);

  fiducialNode->CreateAnnotationTextDisplayNode();
  fiducialNode->CreateAnnotationPointDisplayNode();
  fiducialNode->GetAnnotationPointDisplayNode()->SetGlyphType(vtkMRMLAnnotationPointDisplayNode::Sphere3D);
  fiducialNode->GetAnnotationPointDisplayNode()->SetColor(roiColor);
  fiducialNode->GetAnnotationTextDisplayNode()->SetColor(roiColor);

  fiducialNode->SetDisplayVisibility(0);

  return fiducialNode;
}

//---------------------------------------------------------------------------
vtkMRMLDisplayableNode* vtkSlicerDicomRtImportModuleLogic::AddRoiContour(vtkPolyData *roiPoly, std::string baseName, double *roiColor)
{
  vtkSmartPointer<vtkMRMLModelDisplayNode> displayNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
  displayNode = vtkMRMLModelDisplayNode::SafeDownCast(this->GetMRMLScene()->AddNode(displayNode));
  displayNode->SliceIntersectionVisibilityOn();  
  displayNode->VisibilityOn(); 
  displayNode->SetColor(roiColor[0], roiColor[1], roiColor[2]);

  // Disable backface culling to make the back side of the contour visible as well
  displayNode->SetBackfaceCulling(0);

  std::string modelNodeName = baseName + SlicerRtCommon::CONTOUR_RIBBON_MODEL_NODE_NAME_POSTFIX;
  modelNodeName = this->GetMRMLScene()->GenerateUniqueName(modelNodeName);

  vtkSmartPointer<vtkMRMLModelNode> modelNode = vtkSmartPointer<vtkMRMLModelNode>::New();
  modelNode = vtkMRMLModelNode::SafeDownCast(this->GetMRMLScene()->AddNode(modelNode));
  modelNode->SetName(modelNodeName.c_str());
  modelNode->SetAndObserveDisplayNodeID(displayNode->GetID());
  modelNode->SetAndObservePolyData(roiPoly);
  modelNode->SetHideFromEditors(0);
  modelNode->SetSelectable(1);

  return modelNode;
}
