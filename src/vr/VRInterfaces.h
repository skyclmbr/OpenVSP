//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifndef VRINTERFACES_H
#define VRINTERFACES_H

// Phase 0: stub interfaces; OpenXR and runtime wiring come in later phases.

class VRManager
{
public:
    static VRManager &GetInstance();
    void Init();
    void Shutdown();
private:
    VRManager() = default;
};

class VRRenderer
{
public:
    static VRRenderer &GetInstance();
    void Init();
    void Shutdown();
private:
    VRRenderer() = default;
};

class VRControllerInput
{
public:
    static VRControllerInput &GetInstance();
    void Init();
    void Shutdown();
private:
    VRControllerInput() = default;
};

class VRInteraction
{
public:
    static VRInteraction &GetInstance();
    void Init();
    void Shutdown();
private:
    VRInteraction() = default;
};

class VRRoomScale
{
public:
    static VRRoomScale &GetInstance();
    void Init();
    void Shutdown();
private:
    VRRoomScale() = default;
};

class VRPassthrough
{
public:
    static VRPassthrough &GetInstance();
    void Init();
    void Shutdown();
private:
    VRPassthrough() = default;
};

class VRPanelManager
{
public:
    static VRPanelManager &GetInstance();
    void Init();
    void Shutdown();
private:
    VRPanelManager() = default;
};

#endif
