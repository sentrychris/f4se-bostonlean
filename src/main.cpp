#include "Lean.h"
#include <algorithm>
#include <cmath>

#include <string_view>
#include "RE/C/ConsoleLog.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"

using namespace std::literals;
#define MATH_PI 3.1415926535f
#define HAVOKTOFO4 69.99124f

namespace
{
    bool IsADS(RE::PlayerCharacter* pc)
    {
        return ((int)pc->gunState == 0x8 || (int)pc->gunState == 0x6);
    }

    bool IsFirstPerson(RE::PlayerCamera* pcam)
    {
        // RE::BGSMessage
        return pcam->currentState == pcam->cameraStates[RE::CameraState::kFirstPerson];
    }
}

// Reference: https://github.com/jarari/UneducatedShooter/blob/main/src/main.cpp
// RE::NiNode* InsertBone(RE::NiAVObject* root, RE::NiNode* node, const char* name)
// {
// 	RE::NiNode* parent = node->parent;
// 	RE::NiNode* inserted = (RE::NiNode*)root->GetObjectByName(name);
// 	if (!inserted) {
// 		inserted = RE::CreateBone(name);
// 		//_MESSAGE("%s (%llx) created.", name, inserted);
// 		if (parent) {
// 			parent->AttachChild(inserted, true);
// 			inserted->parent = parent;
// 		} else {
// 			parent = node;
// 		}
// 		inserted->local.translate = RE::NiPoint3();
// 		inserted->local.rotate.MakeIdentity();
// 		inserted->AttachChild(node, true);
// 		return inserted;
// 	} else {
// 		if (!inserted->GetObjectByName(node->name)) {
// 			if (parent) {
// 				parent->AttachChild(inserted, true);
// 				inserted->parent = parent;
// 			} else {
// 				parent = node;
// 			}
// 			inserted->AttachChild(node, true);
// 			return inserted;
// 		}
// 	}
// 	return nullptr;
// }

LeanController& LeanController::Get()
{
    static LeanController instance;
    return instance;
}

void LeanController::resolve_cam_node()
{
    _cam1st = nullptr;
    _fpRoot = nullptr;

    if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
        _fpRoot = pc->Get3D(true);  // true = first-person
        if (_fpRoot) {
            // 1st person skeleton found
            RE::NiNode* chest = (RE::NiNode*)_fpRoot->GetObjectByName("Chest");
            if (chest) {
                // insert a bone
            }

            RE::NiNode* com = (RE::NiNode*)_fpRoot->GetObjectByName("COM");
            if (com) {
                // insert a bone
            }

            RE::NiNode* camera = (RE::NiNode*)_fpRoot->GetObjectByName("Camera");
            if (camera) {
                REX::INFO("camera found");

                _cam1st = _fpRoot->GetObjectByName("Camera");
                _baseCamLocal = _cam1st->local.translate;
                _baseCamRot   = _cam1st->local.rotate;
                _haveCamBasis = true;
            }
        }
    }
}

void LeanController::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    if (!a_event) {
        return;
    }

    // REX::INFO("LeanController OnButtonEvent triggered");

    float axis = _leanAxis;

    const auto code = a_event->GetBSButtonCode();
    const bool isPressed = a_event->QPressed();
    const bool justPressed = a_event->QJustPressed();
    const bool released = a_event->QReleased();

    // REX::INFO("Key {}, isPressed {}, justPressed {}, released {}, axis {}",
    //     static_cast<std::uint32_t>(code), isPressed, justPressed, released, axis);

    if (code == RE::BS_BUTTON_CODE::kQ) {  // Left
        if (isPressed) { // good
            axis = -1.0f;
        }

        if (released) { // good
            axis = 0.0f;
        }
    } else if (code == RE::BS_BUTTON_CODE::kE) {  // Right
        if (isPressed) { // good
            axis = 1.0f;
        }
        if (released) { // good
            axis = 0.0f;
        }
    } else {
        return;
    }

    _leanAxis = std::clamp(axis, -1.0f, 1.0f);
    Update();
}

void LeanController::Update()
{
    auto* pc = RE::PlayerCharacter::GetSingleton();
    auto* pcam = RE::PlayerCamera::GetSingleton();
    if (!pc || !pcam) {
        REX::ERROR("Player character or camera instance not available!");
        return;
    }

    if (!_cam1st) {
        resolve_cam_node();
    }

    if (!_cam1st || !_haveCamBasis) {
        return;
    }

    const bool isAiming1st = IsFirstPerson(pcam) && IsADS(pc);
    const float target = isAiming1st ? _leanAxis : 0.0f;

    // smoothing
    const float dt = 1.0f / 60.0f;
    const float smooth = std::clamp(dt * 15.0f, 0.0f, 1.0f);
    _current += (target - _current) * smooth;

    auto& lt = _cam1st->local;
    lt.translate = RE::NiPoint3{};
    lt.rotate.MakeIdentity();

    constexpr float DEG2RAD       = 0.01745329252f;
    constexpr float kMaxRollDeg   = 69.0f;   // visual lean amount
    constexpr float kPivotHeight  = 14.0f;   // cm above camera origin to emulate neck/eyes
    constexpr float kForwardNudge = 2.5f;    // cm small forward push to clear doorframes

    lt.translate = RE::NiPoint3{};
    lt.rotate.MakeIdentity();

    if (std::abs(_current) > 1e-3f) {
        // Sign: right-lean for positive _current
        const float phi = (_current) * kMaxRollDeg * DEG2RAD;

        const float s = std::sin(phi);
        const float c = std::cos(phi);

        // Arc displacement in the camera's LOCAL space (X=right, Y=fwd, Z=up)
        RE::NiPoint3 deltaLocal(
            kPivotHeight * s,                // right
            kForwardNudge * std::abs(s),     // forward
            kPivotHeight * (c - 1.0f)        // down (negative)
        );

        // Convert to parent space using the camera’s baseline local basis
        RE::NiPoint3 deltaParent = _baseCamRot * deltaLocal;

        lt.translate = deltaParent;

        // If you confirm rotation sticks on your chosen node, you can re-enable this:
        // RE::NiMatrix3 rollM; rollM.FromEulerAnglesXYZ(0.0f, 0.0f, phi);
        // lt.rotate = rollM * _baseCamRot;
    }

    RE::NiUpdateData updateData{};
    _cam1st->Update(updateData);
}

void LeanController::Reset()
{
    _leanAxis = 0.0f;
    _current = 0.0f;
    _cam1st = nullptr;
    _fpRoot = nullptr;
}

namespace
{
    LeanInputHandler* g_inputHandler = nullptr;
}

LeanInputHandler::LeanInputHandler(RE::PlayerControlsData& a_data) : RE::PlayerInputHandler(a_data)
{}

void LeanInputHandler::Install()
{
    if (g_inputHandler) {
        return;
    }

    auto* controls = RE::PlayerControls::GetSingleton();
    if (!controls) {
		REX::ERROR("PlayerControls singleton not ready; input handler not installed.");
        return;
    }

    g_inputHandler = new LeanInputHandler(controls->data);
    controls->RegisterHandler(g_inputHandler);

    LeanController::Get().Reset();

    REX::INFO("Lean input handler registered.");
}

bool LeanInputHandler::ShouldHandleEvent(const RE::InputEvent* a_event)
{
    if (!a_event) {
        return false;
    }

    for (auto* e = a_event; e; e = e->next) {
        if (e->eventType != RE::INPUT_EVENT_TYPE::kButton) {
            // If not button event then continue
            continue;
        }

        const auto* button = e->As<RE::ButtonEvent>();
        if (!button) {
            // If not derived from button event then continue
            continue;
        }

        if (button->device == RE::INPUT_DEVICE::kKeyboard) {
            // Cast the key code and check for Q or E
            const auto kc = static_cast<std::uint32_t>(button->idCode);
            if (kc == 81 || kc == 69) {
                // Q = 81, E = 69
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
	// [28668] [I] LeanInputHandler frame update
    LeanController::Get().Update();
}

void InitPlugin()
{
    if (auto console = RE::ConsoleLog::GetSingleton()) {
        console->PrintLine("Boston Lean v0.0.1 - Player combat leaning mechanics");
        console->PrintLine("By Chris Rowles.");
    }

    LeanInputHandler::Install();

    REX::INFO("plugin initialized.");
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    REL::Trampoline& trampoline = REL::GetTrampoline();
    trampoline.create(static_cast<std::size_t>(64) * 1024);

    if (const auto messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener([](F4SE::MessagingInterface::Message* msg) {
            switch (msg->type) {
            case F4SE::MessagingInterface::kGameDataReady:
                REX::INFO("kGameDataReady event logged");
                InitPlugin();
                break;
            case F4SE::MessagingInterface::kPostLoad:
                REX::INFO("kPostLoad event logged");
                break;
            }
        });
    }

    return true;
}


