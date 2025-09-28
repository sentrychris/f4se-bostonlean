#include "Lean.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "RE/C/ConsoleLog.h"
#include "RE/N/NiUpdateData.h"
#include "RE/N/NiPointer.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"

using namespace std::literals;

namespace
{
    constexpr float kPi = 3.1415926535f;
    constexpr float kDegToRad = kPi / 180.0f;

    constexpr float kLeanDeadzone = 1e-3f;
    constexpr float kLeanEnterSpeed = 12.5f;
    constexpr float kLeanExitSpeed = 10.0f;

    constexpr float kMaxRollDeg = 28.0f;
    constexpr float kMaxYawDeg = 5.0f;
    constexpr float kMaxPitchDeg = 2.5f;

    constexpr float kShoulderShift = 11.0f;
    constexpr float kForwardShift = 4.0f;
    constexpr float kVerticalDrop = 2.5f;

    constexpr float kPivotAnchorMin = 10.0f;

    bool IsFirstPerson(const RE::PlayerCamera& a_camera)
    {
        return a_camera.currentState == a_camera.cameraStates[RE::CameraState::kFirstPerson];
    }
}

LeanController& LeanController::Get()
{
    static LeanController instance;
    return instance;
}

void LeanController::Reset()
{
    _leanAxis = 0.0f;
    _current = 0.0f;
    _leftValue = 0.0f;
    _rightValue = 0.0f;
    _leanLeftHeld = false;
    _leanRightHeld = false;

    _haveCamBasis = false;
    _warnedMissingCamera = false;
    _warnedMissingParent = false;
    _wasLeanAllowed = false;

    ResetRig();
}

void LeanController::ResetRig()
{
    if (_leanPivot) {
        _leanPivot->local.translate = RE::NiPoint3();
        _leanPivot->local.rotate.MakeIdentity();
        RE::NiUpdateData update{};
        _leanPivot->Update(update);
    }

    _leanPivot.reset();
    _camNode.reset();
    _fpRoot.reset();
}

bool LeanController::EnsureRig(const RE::PlayerCharacter& a_player)
{
    RE::NiAVObject* fpRoot = a_player.Get3D(true);
    if (!fpRoot) {
        if (_fpRoot) {
            REX::DEBUG("First-person skeleton unloaded; clearing lean rig.");
        }
        ResetRig();
        return false;
    }

    if (_fpRoot.get() != fpRoot) {
        _fpRoot = fpRoot;
        _camNode.reset();
        _leanPivot.reset();
        _haveCamBasis = false;
        REX::INFO("Acquired first-person skeleton {:p}.", static_cast<void*>(fpRoot));
    }

    if (!_camNode) {
        RE::NiAVObject* cameraNode = _fpRoot->GetObjectByName("Camera");
        if (!cameraNode) {
            if (!_warnedMissingCamera) {
                _warnedMissingCamera = true;
                REX::WARN("First-person Camera node not found; leaning disabled until skeleton resolves.");
            }
            return false;
        }

        _warnedMissingCamera = false;
        _camNode = cameraNode;
        _cameraRestTranslation = _camNode->local.translate;
        _cameraRestRotation = _camNode->local.rotate;
        _camLocalZ0 = _cameraRestTranslation.z;
        const float anchorZ = std::max(_camLocalZ0 * 0.45f, kPivotAnchorMin);
        _pivotAnchor = { 0.0f, 0.0f, anchorZ };
        _haveCamBasis = true;

        REX::INFO("Camera basis cached (local z {:.2f}, pivot anchor {:.2f}).", _camLocalZ0, anchorZ);
    }

    if (_leanPivot && _camNode && _camNode->parent == _leanPivot.get()) {
        return true;
    }

    if (!_camNode) {
        return false;
    }

    RE::NiNode* parent = _camNode->parent;
    if (!parent) {
        if (!_warnedMissingParent) {
            _warnedMissingParent = true;
            REX::WARN("Camera node has no parent; unable to insert lean pivot.");
        }
        return false;
    }
    _warnedMissingParent = false;
    RE::NiPointer<RE::NiAVObject> cameraPtr;
    parent->DetachChild(_camNode.get(), cameraPtr);
    if (!cameraPtr) {
        REX::ERROR("Failed to detach Camera when inserting lean pivot.");
        return false;
    }
    auto pivot = RE::NiPointer<RE::NiNode>(new RE::NiNode());
    pivot->local.translate = RE::NiPoint3();
    pivot->local.rotate.MakeIdentity();
    pivot->AttachChild(cameraPtr.get(), true);

    parent->AttachChild(pivot.get(), true);
    _leanPivot = pivot;
    _camNode = cameraPtr;

    REX::INFO("Lean pivot inserted under %s.", parent->name.c_str());

    return true;
}

bool LeanController::CanLean(const RE::PlayerCharacter& a_player, const RE::PlayerCamera& a_camera)
{
    if (!IsFirstPerson(a_camera)) {
        return false;
    }

    if (const auto* ui = RE::UI::GetSingleton(); ui && ui->menuMode != 0) {
        return false;
    }

    (void)a_player;
    return _haveCamBasis && _leanPivot;
}

void LeanController::RestoreCameraBasis()
{
    if (_camNode && _haveCamBasis) {
        _camNode->local.translate = _cameraRestTranslation;
        _camNode->local.rotate = _cameraRestRotation;
    }
}

void LeanController::ApplyLean(float a_amount)
{
    if (!_leanPivot) {
        return;
    }

    RestoreCameraBasis();

    if (std::fabs(a_amount) <= kLeanDeadzone) {
        _leanPivot->local.translate = RE::NiPoint3();
        _leanPivot->local.rotate.MakeIdentity();
        RE::NiUpdateData update{};
        _leanPivot->Update(update);
        return;
    }

    const float weight = std::clamp(std::fabs(a_amount), 0.0f, 1.0f);
    const float rollRad = a_amount * kMaxRollDeg * kDegToRad;
    const float yawRad = a_amount * kMaxYawDeg * kDegToRad;
    const float pitchRad = -weight * kMaxPitchDeg * kDegToRad;

    RE::NiMatrix3 rot;
    rot.FromEulerAnglesXYZ(pitchRad, yawRad, rollRad);
    _leanPivot->local.rotate = rot;

    const RE::NiPoint3 rotatedAnchor = rot * _pivotAnchor;
    RE::NiPoint3 translation = _pivotAnchor - rotatedAnchor;

    translation.x += kShoulderShift * a_amount;
    translation.y += kForwardShift * weight;
    translation.z -= kVerticalDrop * weight;

    _leanPivot->local.translate = translation;

    RE::NiUpdateData update{};
    _leanPivot->Update(update);
}

void LeanController::RecalculateAxis()
{
    float axis = 0.0f;
    const float left = std::clamp(_leftValue, 0.0f, 1.0f);
    const float right = std::clamp(_rightValue, 0.0f, 1.0f);

    if (_leanLeftHeld && !_leanRightHeld) {
        axis = -left;
    } else if (_leanRightHeld && !_leanLeftHeld) {
        axis = right;
    } else if (_leanRightHeld && _leanLeftHeld) {
        axis = right - left;
    }

    axis = std::clamp(axis, -1.0f, 1.0f);

    if (std::fabs(axis - _leanAxis) > 1e-3f) {
        REX::DEBUG("Lean axis updated to {:.3f} (left {:.2f}, right {:.2f}).", axis, left, right);
    }

    _leanAxis = axis;
}

void LeanController::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    if (!a_event) {
        return;
    }

    const auto code = a_event->GetBSButtonCode();
    const float analog = std::clamp(a_event->QAnalogValue(), 0.0f, 1.0f);
    const bool pressed = analog > 0.0f;

    switch (code) {
    case RE::BS_BUTTON_CODE::kQ:
        _leanLeftHeld = pressed;
        _leftValue = pressed ? analog : 0.0f;
        REX::DEBUG("Lean left input {} (analog {:.2f}).", pressed ? "pressed" : "released", analog);
        break;
    case RE::BS_BUTTON_CODE::kE:
        _leanRightHeld = pressed;
        _rightValue = pressed ? analog : 0.0f;
        REX::DEBUG("Lean right input {} (analog {:.2f}).", pressed ? "pressed" : "released", analog);
        break;
    default:
        return;
    }

    RecalculateAxis();
}

void LeanController::Update()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!player || !camera) {
        return;
    }

    const bool rigReady = EnsureRig(*player);
    const bool allowLean = rigReady && CanLean(*player, *camera);

    if (_wasLeanAllowed != allowLean) {
        _wasLeanAllowed = allowLean;
        REX::DEBUG("Lean availability {}.", allowLean ? "enabled" : "disabled");
    }

    const float target = allowLean ? _leanAxis : 0.0f;
    const float speed = std::fabs(target) > std::fabs(_current) ? kLeanEnterSpeed : kLeanExitSpeed;
    const float alpha = std::clamp(speed * (1.0f / 60.0f), 0.0f, 1.0f);

    _current += (target - _current) * alpha;

    if (std::fabs(_current) <= kLeanDeadzone) {
        _current = 0.0f;
    }

    ApplyLean(_current);
}

namespace
{
    LeanInputHandler* g_inputHandler = nullptr;
}

LeanInputHandler::LeanInputHandler(RE::PlayerControlsData& a_data) :
    RE::PlayerInputHandler(a_data)
{}

void LeanInputHandler::Install()
{
    if (g_inputHandler) {
        return;
    }

    auto* controls = RE::PlayerControls::GetSingleton();
    if (!controls) {
        REX::ERROR("PlayerControls singleton not ready; lean input handler not installed.");
        return;
    }

    g_inputHandler = new LeanInputHandler(controls->data);
    controls->RegisterHandler(g_inputHandler);

    LeanController::Get().Reset();

    REX::INFO("Lean input handler registered.");
}

bool LeanInputHandler::ShouldHandleEvent(const RE::InputEvent* a_event)
{
    for (auto* e = a_event; e; e = e->next) {
        if (const auto* button = e->As<RE::ButtonEvent>()) {
            const auto code = button->GetBSButtonCode();
            if (code == RE::BS_BUTTON_CODE::kQ || code == RE::BS_BUTTON_CODE::kE) {
                return true;
            }
        }
    }

    return false;
}

void LeanInputHandler::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    LeanController::Get().OnButtonEvent(a_event);
}

void LeanInputHandler::PerFrameUpdate()
{
    LeanController::Get().Update();
}

void InitPlugin()
{
    if (auto* console = RE::ConsoleLog::GetSingleton()) {
        console->PrintLine("Boston Lean v0.1.0 - First-person leaning overhaul");
        console->PrintLine("By Chris Rowles.");
    }

    LeanInputHandler::Install();

    REX::INFO("Plugin initialised.");
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    REL::Trampoline& trampoline = REL::GetTrampoline();
    trampoline.create(static_cast<std::size_t>(64) * 1024);

    if (const auto* messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener([](F4SE::MessagingInterface::Message* msg) {
            switch (msg->type) {
            case F4SE::MessagingInterface::kGameDataReady:
                REX::INFO("kGameDataReady event received.");
                InitPlugin();
                break;
            case F4SE::MessagingInterface::kPostLoad:
                REX::INFO("kPostLoad event received.");
                break;
            case F4SE::MessagingInterface::kPostLoadGame:
            case F4SE::MessagingInterface::kNewGame:
                REX::INFO("Reload event received; resetting lean controller state.");
                LeanController::Get().Reset();
                break;
            default:
                break;
            }
        });
    }

    return true;
}







