#pragma once

#include "RE/B/ButtonEvent.h"
#include "RE/I/InputEvent.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiPointer.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/PlayerInputHandler.h"


class LeanController final
{
public:
    static LeanController& Get();

    void OnButtonEvent(const RE::ButtonEvent* a_event);
    void Update();
    void Reset();

private:
    LeanController() = default;
    void resolve_cam_node();

    float _leanAxis = 0.0f;
    float _current = 0.0f;
    RE::NiAVObject* _cam1st = nullptr;
    RE::NiPointer<RE::NiAVObject> _fpRoot;

    RE::NiPoint3  _baseCamLocal{};
    RE::NiMatrix3 _baseCamRot{};
    bool _haveCamBasis{false};

    RE::NiNode*   _camNode = nullptr;     // "Camera" NiNode
    RE::NiNode*   _leanPivot = nullptr;   // inserted parent we control
    float         _camLocalZ0 = 0.0f;     // baseline camera local Z (for pivot height)
    bool          _haveRig = false;
};

class LeanInputHandler final : public RE::PlayerInputHandler
{
public:
    static void Install();

    bool ShouldHandleEvent(const RE::InputEvent* a_event) override;
    void OnButtonEvent(const RE::ButtonEvent* a_event) override;
    void PerFrameUpdate() override;

private:
    explicit LeanInputHandler(RE::PlayerControlsData& a_data);
};
