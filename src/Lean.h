#pragma once

#include "RE/B/ButtonEvent.h"
#include "RE/I/InputEvent.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiPointer.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/PlayerInputHandler.h"

namespace RE
{
    class PlayerCamera;
    class PlayerCharacter;
}

class LeanController final
{
public:
    static LeanController& Get();

    void OnButtonEvent(const RE::ButtonEvent* a_event);
    void Update();
    void Reset();

private:
    LeanController() = default;

    void ResetRig();
    bool EnsureRig(const RE::PlayerCharacter& a_player);
    bool CanLean(const RE::PlayerCharacter& a_player, const RE::PlayerCamera& a_camera);
    void ApplyLean(float a_amount);
    void RestoreCameraBasis();
    void RecalculateAxis();

    float _leanAxis = 0.0f;      // desired target based on inputs (-1 = left, +1 = right)
    float _current = 0.0f;       // smoothed lean weight
    float _leftValue = 0.0f;     // analog value for left input
    float _rightValue = 0.0f;    // analog value for right input
    bool _leanLeftHeld = false;
    bool _leanRightHeld = false;

    RE::NiPointer<RE::NiAVObject> _fpRoot;
    RE::NiPointer<RE::NiAVObject> _camNode;    // cached pointer to first-person camera node
    RE::NiPointer<RE::NiNode>     _leanPivot;  // inserted pivot parent we author

    RE::NiPoint3  _cameraRestTranslation{};
    RE::NiMatrix3 _cameraRestRotation{};
    RE::NiPoint3  _pivotAnchor{ 0.0f, 0.0f, 18.0f };

    float _camLocalZ0 = 0.0f;

    bool _haveCamBasis = false;
    bool _warnedMissingCamera = false;
    bool _warnedMissingParent = false;
    bool _wasLeanAllowed = false;
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

