/*==============================================================================

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
  and was supported through CANARIE's Research Software Program, and Cancer
  Care Ontario.

==============================================================================*/

#include "vtkVirtualRealityViewInteractorStyle.h"

// VR MRML includes
#include "vtkMRMLVirtualRealityViewNode.h"

// MRML includes
#include "vtkMRMLScene.h"
#include "vtkMRMLModelNode.h"
#include "vtkMRMLModelDisplayNode.h"
#include "vtkMRMLVolumeNode.h"
#include "vtkMRMLVolumeRenderingDisplayNode.h"
#include "vtkMRMLSegmentationNode.h"
#include "vtkMRMLSegmentationDisplayNode.h"
#include "vtkMRMLLinearTransformNode.h"
#include "vtkMRMLAbstractThreeDViewDisplayableManager.h"
#include "vtkMRMLModelDisplayableManager.h"

// VTK includes
#include <vtkCamera.h>
#include <vtkCellPicker.h>
#include <vtkCallbackCommand.h>
#include <vtkMath.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkRenderer.h>
#include "vtkQuaternion.h"
#include <vtkWeakPointer.h>

#include "vtkOpenVRRenderWindow.h"
#include "vtkOpenVRRenderWindowInteractor.h"


vtkStandardNewMacro(vtkVirtualRealityViewInteractorStyle);

//----------------------------------------------------------------------------
class vtkVirtualRealityViewInteractorStyle::vtkInternal
{
public:
  vtkInternal();
  ~vtkInternal();

public:
  // Store required controllers information when performing action
  int InteractionState[vtkEventDataNumberOfDevices];

  vtkWeakPointer<vtkMRMLDisplayableNode> PickedNode[vtkEventDataNumberOfDevices];

  //vtkPlane* ClippingPlanes[vtkEventDataNumberOfDevices];
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkVirtualRealityViewInteractorStyle::vtkInternal::vtkInternal()
{
  for (int d = 0; d < vtkEventDataNumberOfDevices; ++d)
  {
    this->InteractionState[d] = VTKIS_NONE;
    this->PickedNode[d] = nullptr;
    //this->ClippingPlanes[d] = nullptr;
  }
}

//---------------------------------------------------------------------------
vtkVirtualRealityViewInteractorStyle::vtkInternal::~vtkInternal()
{
}

//---------------------------------------------------------------------------
// vtkVirtualRealityViewInteractorStyle methods

//----------------------------------------------------------------------------
vtkVirtualRealityViewInteractorStyle::vtkVirtualRealityViewInteractorStyle()
  : DisplayableManagerGroup(nullptr)
{
  this->Internal = new vtkInternal();

  for (int d = 0; d < vtkEventDataNumberOfDevices; ++d)
  {
    this->Internal->InteractionState[d] = VTKIS_NONE;

    for (int i = 0; i < vtkEventDataNumberOfInputs; i++)
    {
      this->InputMap[d][i] = -1;
    }
  }

  // Create default inputs mapping
  this->MapInputToAction(vtkEventDataDevice::RightController,
    vtkEventDataDeviceInput::Grip, VTKIS_POSITION_PROP);
  this->MapInputToAction(vtkEventDataDevice::RightController,
    vtkEventDataDeviceInput::TrackPad, VTKIS_DOLLY);

  this->MapInputToAction(vtkEventDataDevice::LeftController,
    vtkEventDataDeviceInput::Grip, VTKIS_POSITION_PROP);
}

//----------------------------------------------------------------------------
vtkVirtualRealityViewInteractorStyle::~vtkVirtualRealityViewInteractorStyle()
{
  this->SetDisplayableManagerGroup(nullptr);
  delete this->Internal;
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
} 

//----------------------------------------------------------------------------
// Generic events binding
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::OnMove3D(vtkEventData* edata)
{
  vtkEventDataDevice3D *edd = edata->GetAsEventDataDevice3D();
  if (!edd)
  {
    return;
  }
  int idev = static_cast<int>(edd->GetDevice());

  // Update current state
  int x = this->Interactor->GetEventPosition()[0];
  int y = this->Interactor->GetEventPosition()[1];

  switch (this->Internal->InteractionState[idev])
  {
    case VTKIS_POSITION_PROP:
      this->FindPokedRenderer(x, y);
      this->PositionProp(edd);
      this->InvokeEvent(vtkCommand::InteractionEvent, nullptr);
      break;
    case VTKIS_DOLLY:
      this->FindPokedRenderer(x, y);
      this->Dolly3D(edata);
      this->InvokeEvent(vtkCommand::InteractionEvent, nullptr);
      break;
    //case VTKIS_CLIP:
    //  this->FindPokedRenderer(x, y);
    //  this->Clip(edd);
    //  this->InvokeEvent(vtkCommand::InteractionEvent, nullptr);
    //  break;
  }

  //// Update rays
  //if (this->HoverPick)
  //{
  //  this->UpdateRay(edd->GetDevice());
  //}
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::OnButton3D(vtkEventData* edata)
{
  vtkEventDataDevice3D *bd = edata->GetAsEventDataDevice3D();
  if (!bd)
  {
    return;
  }

  int x = this->Interactor->GetEventPosition()[0];
  int y = this->Interactor->GetEventPosition()[1];
  this->FindPokedRenderer(x, y);

  int state = this->InputMap[static_cast<int>(bd->GetDevice())][static_cast<int>(bd->GetInput())];
  if (state == -1)
  {
    return;
  }

  // Right trigger press/release
  if (bd->GetAction() == vtkEventDataAction::Press)
  {
    this->StartAction(state, bd);
  }
  if (bd->GetAction() == vtkEventDataAction::Release)
  {
    this->EndAction(state, bd);
  }
}

//----------------------------------------------------------------------------
// Interaction methods
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::PositionProp(vtkEventData* ed)
{
  vtkEventDataDevice3D *edd = ed->GetAsEventDataDevice3D();
  if (!edd)
  {
    return;
  }
  int deviceIndex = static_cast<int>(edd->GetDevice());
  vtkMRMLDisplayableNode* pickedNode = this->Internal->PickedNode[deviceIndex];

  if ( pickedNode == nullptr || !pickedNode->GetSelectable()
    || !this->CurrentRenderer || !this->DisplayableManagerGroup )
  {
    return;
  }
  if (ed->GetType() != vtkCommand::Move3DEvent)
  {
    return;
  }

  // Get positions and orientations
  vtkRenderWindowInteractor3D* rwi = static_cast<vtkRenderWindowInteractor3D*>(this->Interactor);
  double worldPos[3] = {0.0};
  edd->GetWorldPosition(worldPos);
  double* lastWorldPos = rwi->GetLastWorldEventPosition(rwi->GetPointerIndex());
  double* worldOrientation = rwi->GetWorldEventOrientation(rwi->GetPointerIndex());
  double* lastWorldOrientation = rwi->GetLastWorldEventOrientation(rwi->GetPointerIndex());

  // Calculate transform
  vtkNew<vtkTransform> interactionTransform;
  interactionTransform->PreMultiply();

  double translation[3] = {0.0};
  for (int i = 0; i < 3; i++)
  {
    translation[i] = worldPos[i] - lastWorldPos[i];
  }
  vtkQuaternion<double> q1;
  q1.SetRotationAngleAndAxis(vtkMath::RadiansFromDegrees(lastWorldOrientation[0]), lastWorldOrientation[1], lastWorldOrientation[2], lastWorldOrientation[3]);
  vtkQuaternion<double> q2;
  q2.SetRotationAngleAndAxis(vtkMath::RadiansFromDegrees(worldOrientation[0]), worldOrientation[1], worldOrientation[2], worldOrientation[3]);
  q1.Conjugate();
  q2 = q2*q1;
  double axis[4] = {0.0};
  axis[0] = vtkMath::DegreesFromRadians(q2.GetRotationAngleAndAxis(axis+1));
  interactionTransform->Translate(worldPos[0], worldPos[1], worldPos[2]);
  interactionTransform->RotateWXYZ(axis[0], axis[1], axis[2], axis[3]);
  interactionTransform->Translate(-worldPos[0], -worldPos[1], -worldPos[2]);
  interactionTransform->Translate(translation[0], translation[1], translation[2]);

  // Make sure that the topmost parent transform is the VR interaction transform
  vtkMRMLTransformNode* topTransformNode = pickedNode->GetParentTransformNode();
  while (topTransformNode && topTransformNode->GetParentTransformNode())
  {
    topTransformNode = topTransformNode->GetParentTransformNode();
  }
  vtkMRMLTransformNode* vrTransformNode = NULL;
  if ( !topTransformNode
    || !topTransformNode->GetAttribute(vtkMRMLVirtualRealityViewNode::GetVirtualRealityInteractionTransformAttributeName()) )
  {
    // Create interaction transform if not found
    vtkNew<vtkMRMLTransformNode> newTransformNode;
    std::string vrTransformNodeName(pickedNode->GetName());
    vrTransformNodeName.append("_VR_Interaction_Transform");
    newTransformNode->SetName(vrTransformNodeName.c_str());
    newTransformNode->SetAttribute(vtkMRMLVirtualRealityViewNode::GetVirtualRealityInteractionTransformAttributeName(), "1");
    pickedNode->GetScene()->AddNode(newTransformNode);
    if (topTransformNode)
    {
      topTransformNode->SetAndObserveTransformNodeID(newTransformNode->GetID());
    }
    else
    {
      pickedNode->SetAndObserveTransformNodeID(newTransformNode->GetID());
    }
    vrTransformNode = newTransformNode.GetPointer();
  }
  else
  {
    // VR interaction transform found (i.e. the top transform has the VR interaction transform attribute)
    vrTransformNode = topTransformNode;
  }

  // Apply interaction transform
  vtkTransform* lastVrInteractionTransform = vtkTransform::SafeDownCast(vrTransformNode->GetTransformToParent());
  if (vrTransformNode->GetTransformToParent() && !lastVrInteractionTransform)
  {
    //TODO: Remove constraint
    vtkErrorMacro("PositionProp: Node " << pickedNode->GetName() << " contains non-linear transform");
    return;
  }
  // Use the interaction transform with the flattened matrix to prevent concatenation
  // of potentially thousands of transforms
  if (lastVrInteractionTransform)
  {
    interactionTransform->Concatenate(lastVrInteractionTransform->GetMatrix());
  }
  vrTransformNode->SetAndObserveTransformToParent(interactionTransform);

  if (this->AutoAdjustCameraClippingRange)
  {
    this->CurrentRenderer->ResetCameraClippingRange();
  }
}

//----------------------------------------------------------------------------
// Interaction entry points
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::StartPositionProp(vtkEventDataDevice3D* edata)
{
  vtkMRMLScene* scene = this->GetMRMLScene();
  if (!scene)
  {
    return;
  }

  int deviceIndex = static_cast<int>(edata->GetDevice());

  double pos[3] = {0.0};
  edata->GetWorldPosition(pos);

  // Get MRML node to move
  vtkMRMLDisplayableNode* pickedNode = NULL;
  for (int i=0; i<this->DisplayableManagerGroup->GetDisplayableManagerCount(); ++i)
  {
    vtkMRMLAbstractThreeDViewDisplayableManager* currentDisplayableManager =
      vtkMRMLAbstractThreeDViewDisplayableManager::SafeDownCast(this->DisplayableManagerGroup->GetNthDisplayableManager(i));
    if (!currentDisplayableManager)
    {
      continue;
    }
    currentDisplayableManager->Pick3D(pos);
    vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(
      scene->GetNodeByID(currentDisplayableManager->GetPickedNodeID()) );
    if (!displayNode)
    {
      continue;
    }
    //TODO: Only the first picked node in the last displayable manager will be picked
    this->Internal->PickedNode[deviceIndex] = displayNode->GetDisplayableNode();
  }

  this->Internal->InteractionState[deviceIndex] = VTKIS_POSITION_PROP;

  // Don't start action if a controller is already positioning the prop
  int rc = static_cast<int>(vtkEventDataDevice::RightController);
  int lc = static_cast<int>(vtkEventDataDevice::LeftController);
  if (this->Internal->PickedNode[rc] == this->Internal->PickedNode[lc])
  {
    this->EndPositionProp(edata);
    return;
  }

  if (this->CurrentRenderer == nullptr || this->InteractionProp == nullptr)
  {
    return;
  }
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::EndPositionProp(vtkEventDataDevice3D* edata)
{
  int deviceIndex = static_cast<int>(edata->GetDevice());
  this->Internal->InteractionState[deviceIndex] = VTKIS_NONE;
  this->Internal->PickedNode[deviceIndex] = nullptr;
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::StartDolly3D(vtkEventDataDevice3D* ed)
{
  if (this->CurrentRenderer == nullptr)
  {
    return;
  }
  vtkEventDataDevice dev = ed->GetDevice();
  this->Internal->InteractionState[static_cast<int>(dev)] = VTKIS_DOLLY;

  // this->GrabFocus(this->EventCallbackCommand);
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::EndDolly3D(vtkEventDataDevice3D* ed)
{
  vtkEventDataDevice dev = ed->GetDevice();
  this->Internal->InteractionState[static_cast<int>(dev)] = VTKIS_NONE;
}

//----------------------------------------------------------------------------
// Multitouch interaction methods
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::OnPan()
{
  int rc = static_cast<int>(vtkEventDataDevice::RightController);
  int lc = static_cast<int>(vtkEventDataDevice::LeftController);

  if (!this->Internal->PickedNode[rc] && !this->Internal->PickedNode[lc])
  {
    this->Internal->InteractionState[rc] = VTKIS_PAN;
    this->Internal->InteractionState[lc] = VTKIS_PAN;

    int pointer = this->Interactor->GetPointerIndex();

    this->FindPokedRenderer(this->Interactor->GetEventPositions(pointer)[0],
      this->Interactor->GetEventPositions(pointer)[1]);

    if (this->CurrentRenderer == nullptr)
    {
      return;
    }

    vtkCamera *camera = this->CurrentRenderer->GetActiveCamera();
    vtkRenderWindowInteractor3D *rwi =
      static_cast<vtkRenderWindowInteractor3D *>(this->Interactor);

    double t[3] = {
      rwi->GetTranslation3D()[0] - rwi->GetLastTranslation3D()[0],
      rwi->GetTranslation3D()[1] - rwi->GetLastTranslation3D()[1],
      rwi->GetTranslation3D()[2] - rwi->GetLastTranslation3D()[2]};

    double *ptrans = rwi->GetPhysicalTranslation(camera);
    double distance = rwi->GetPhysicalScale();

    //TODO: Act on the matrix that is currently the variables like PhysicalViewUp etc.
    rwi->SetPhysicalTranslation(camera,
      ptrans[0] + t[0] * distance,
      ptrans[1] + t[1] * distance,
      ptrans[2] + t[2] * distance);

    // clean up
    if (this->Interactor->GetLightFollowCamera())
    {
      this->CurrentRenderer->UpdateLightsGeometryToFollowCamera();
    }
  }
}
//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::OnPinch()
{
  int rc = static_cast<int>(vtkEventDataDevice::RightController);
  int lc = static_cast<int>(vtkEventDataDevice::LeftController);

  if (!this->Internal->PickedNode[rc] && !this->Internal->PickedNode[lc])
  {
    this->Internal->InteractionState[rc] = VTKIS_ZOOM;
    this->Internal->InteractionState[lc] = VTKIS_ZOOM;

    int pointer = this->Interactor->GetPointerIndex();

    this->FindPokedRenderer(this->Interactor->GetEventPositions(pointer)[0],
      this->Interactor->GetEventPositions(pointer)[1]);

    if (this->CurrentRenderer == nullptr)
    {
      return;
    }

    double dyf = this->Interactor->GetScale() / this->Interactor->GetLastScale();
    vtkCamera *camera = this->CurrentRenderer->GetActiveCamera();
    vtkRenderWindowInteractor3D *rwi =
      static_cast<vtkRenderWindowInteractor3D *>(this->Interactor);
    double distance = rwi->GetPhysicalScale();

    this->SetScale(camera, distance / dyf);
  }
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::OnRotate()
{
  int rc = static_cast<int>(vtkEventDataDevice::RightController);
  int lc = static_cast<int>(vtkEventDataDevice::LeftController);

  // Rotate only when one controller is not interacting
  if ( (this->Internal->PickedNode[rc] || this->Internal->PickedNode[lc])
    && (!this->Internal->PickedNode[rc] || !this->Internal->PickedNode[lc]) )
  {
    this->Internal->InteractionState[rc] = VTKIS_ROTATE;
    this->Internal->InteractionState[lc] = VTKIS_ROTATE;

    double angle = this->Interactor->GetRotation() - this->Interactor->GetLastRotation();

    //TODO: Rotate the world instead of the node (the matrix that is currently the variables like PhysicalViewUp etc.)
    if (this->Internal->PickedNode[rc])
    {
      int i=0; ++i;
      //this->InteractionProps[rc]->RotateY(angle);
    }
    if (this->Internal->PickedNode[lc])
    {
      int i=0; ++i;
      //this->InteractionProps[lc]->RotateY(angle);
    }
  }
}

//----------------------------------------------------------------------------
// Utility routines
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::MapInputToAction(
  vtkEventDataDevice device, vtkEventDataDeviceInput input, int state)
{
  if (input >= vtkEventDataDeviceInput::NumberOfInputs || state < VTKIS_NONE)
  {
    return;
  }

  int oldstate = this->InputMap[static_cast<int>(device)][static_cast<int>(input)];
  if (oldstate == state)
  {
    return;
  }

  this->InputMap[static_cast<int>(device)][static_cast<int>(input)] = state;
  //this->AddTooltipForInput(device, input); //TODO:

  this->Modified();
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::StartAction(int state, vtkEventDataDevice3D* edata)
{
  switch (state)
  {
    case VTKIS_POSITION_PROP:
      this->StartPositionProp(edata);
      break;
    case VTKIS_DOLLY:
      this->StartDolly3D(edata);
      break;
    //case VTKIS_CLIP:
    //  this->StartClip(edata);
    //  break;
    //case VTKIS_PICK:
    //  this->StartPick(edata);
    //  break;
    //case VTKIS_LOAD_CAMERA_POSE:
    //  this->StartLoadCamPose(edata);
    //  break;
  }
}

//----------------------------------------------------------------------------
void vtkVirtualRealityViewInteractorStyle::EndAction(int state, vtkEventDataDevice3D* edata)
{
  switch (state)
  {
    case VTKIS_POSITION_PROP:
      this->EndPositionProp(edata);
      break;
    case VTKIS_DOLLY:
      this->EndDolly3D(edata);
      break;
    //case VTKIS_CLIP:
    //  this->EndClip(edata);
    //  break;
    //case VTKIS_PICK:
    //  this->EndPick(edata);
    //  break;
    //case VTKIS_MENU:
    //  this->Menu->SetInteractor(this->Interactor);
    //  this->Menu->Show(edata);
    //  break;
    //case VTKIS_LOAD_CAMERA_POSE:
    //  this->EndLoadCamPose(edata);
    //  break;
    //case VTKIS_TOGGLE_DRAW_CONTROLS:
    //  this->ToggleDrawControls();
    //  break;
    //case VTKIS_EXIT:
    //  if (this->Interactor)
    //  {
    //    this->Interactor->ExitCallback();
    //  }
    //  break;
  }

  // Reset multitouch state because a button has been released
  //for (int d = 0; d < vtkEventDataNumberOfDevices; ++d)
  //{
  //  switch (this->InteractionState[d])
  //  {
  //    case VTKIS_PAN:
  //    case VTKIS_ZOOM:
  //    case VTKIS_ROTATE:
  //      this->InteractionState[d] = VTKIS_NONE;
  //      break;
  //  }
  //}
}

//---------------------------------------------------------------------------
vtkMRMLScene* vtkVirtualRealityViewInteractorStyle::GetMRMLScene()
{
  if (!this->DisplayableManagerGroup
    || this->DisplayableManagerGroup->GetDisplayableManagerCount() == 0)
  {
    return nullptr;
  }

  return this->DisplayableManagerGroup->GetNthDisplayableManager(0)->GetMRMLScene();
}
