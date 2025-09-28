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
        return pcam->currentState == pcam->cameraStates[RE::CameraState::kFirstPerson];
    }
}

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
            _cam1st = _fpRoot->GetObjectByName("Camera1st [Cam1]"sv);
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

    REX::INFO("Key {}, isPressed {}, justPressed {}, released {}, axis {}",
        static_cast<std::uint32_t>(code), isPressed, justPressed, released, axis);

    const auto* button = a_event->As<RE::ButtonEvent>();
    if (!button) {
        // If not derived from button event then continue
        return;
    }

    if (button->device == RE::INPUT_DEVICE::kKeyboard) {
        // Cast the key code and check for Q or E
        const auto kc = static_cast<std::uint32_t>(button->idCode);
        if (kc == 81) {
            // cool
        }
    }

    if (code == RE::BS_BUTTON_CODE::kQ) {  // Left
        if (justPressed || isPressed) {
            axis = -1.0f;
        }
        if (released) {
            axis = 0.0f;
        }
    } else if (code == RE::BS_BUTTON_CODE::kE) {  // Right
        if (justPressed || isPressed) {
            axis = 1.0f;
        }
        if (released) {
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
        return;
    }

    if (!_cam1st || !_fpRoot) {
        resolve_cam_node();
    }

    if (!_cam1st) {
        return;
    }

    const bool isAiming1st = IsFirstPerson(pcam) && IsADS(pc);
    const float target = isAiming1st ? _leanAxis : 0.0f;

    const float dt = 1.0f / 60.0f;  // Assume 60FPS
    const float smooth = std::clamp(dt * 15.0f, 0.0f, 1.0f);
    _current += (target - _current) * smooth;

    constexpr float kMaxOffset = 7.5f;  // cm
    constexpr float kMaxRoll = 6.0f;    // deg

    auto& lt = _cam1st->local;
    lt.translate = RE::NiPoint3{};
    lt.rotate = RE::NiMatrix3();

    if (std::abs(_current) > 1e-3f) {
        const float offset = kMaxOffset * _current;
        const float rollRad = (kMaxRoll * -_current) * (MATH_PI / 180.0f);

        lt.translate.x += offset;

        RE::NiMatrix3 rot;
        rot.FromEulerAnglesXYZ(0.0f, 0.0f, rollRad);
        lt.rotate = rot;
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

namespace
{
    void InitPlugin()
    {
        if (auto console = RE::ConsoleLog::GetSingleton()) {
            console->PrintLine("Boston Lean v0.0.1 - Player combat leaning mechanics");
            console->PrintLine("By Chris Rowles.");
        }

        REX::INFO("plugin initialized.");
    }
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
                LeanInputHandler::Install();
                break;
            case F4SE::MessagingInterface::kPostLoad:
                REX::INFO("kPostLoad event logged");
                break;
            }
        });
    }

    return true;
}


