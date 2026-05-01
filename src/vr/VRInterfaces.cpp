//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VRInterfaces.h"

VRManager &VRManager::GetInstance()
{
    static VRManager inst;
    return inst;
}

void VRManager::Init() {}
void VRManager::Shutdown() {}

VRRenderer &VRRenderer::GetInstance()
{
    static VRRenderer inst;
    return inst;
}

void VRRenderer::Init() {}
void VRRenderer::Shutdown() {}

VRControllerInput &VRControllerInput::GetInstance()
{
    static VRControllerInput inst;
    return inst;
}

void VRControllerInput::Init() {}
void VRControllerInput::Shutdown() {}

VRInteraction &VRInteraction::GetInstance()
{
    static VRInteraction inst;
    return inst;
}

void VRInteraction::Init() {}
void VRInteraction::Shutdown() {}

VRRoomScale &VRRoomScale::GetInstance()
{
    static VRRoomScale inst;
    return inst;
}

void VRRoomScale::Init() {}
void VRRoomScale::Shutdown() {}

VRPassthrough &VRPassthrough::GetInstance()
{
    static VRPassthrough inst;
    return inst;
}

void VRPassthrough::Init() {}
void VRPassthrough::Shutdown() {}

VRPanelManager &VRPanelManager::GetInstance()
{
    static VRPanelManager inst;
    return inst;
}

void VRPanelManager::Init() {}
void VRPanelManager::Shutdown() {}
