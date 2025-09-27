#include "Lean.h"

#define MATH_PI 3.1415926535f
#define HAVOKTOFO4 69.99124f

#pragma region Utils

bool IsADS(RE::PlayerCharacter* pc)
{
	return ((int)pc->gunState == 0x8 || (int)pc->gunState == 0x6);
}

bool IsFirstPerson(RE::PlayerCamera* pcam)
{
	return pcam->currentState == pcam->cameraStates[RE::CameraState::kFirstPerson];
}

#pragma endregion

#pragma region Hooks

namespace Hooks
{
    using SinkFn = RE::BSEventNotifyControl(
        void*,
        RE::InputEvent* const&,
        RE::BSTEventSource<RE::InputEvent*>*
	);

    using FnPtr = SinkFn*;

	template <class T, std::size_t VIndex>

    struct InputSinkHook
    {
        static inline FnPtr orig = nullptr;  // original function pointer we replace

        static RE::BSEventNotifyControl thunk(
            void* a_this,
            RE::InputEvent* const& a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_src)
        {
            // Uncomment for proof:
            REX::INFO("[BostonLean] THUNK fired: {} VTABLE[{}]", typeid(T).name(), VIndex);

            const auto ret = orig(a_this, a_event, a_src);      // call game’s original
            LeanInputSink::Get().ProcessEvent(a_event, a_src);  // then ours
            return ret;
        }

        static bool install(const char* name)
        {
            try {
                // Resolve the vtable base for this class/index
                REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[VIndex] };
                auto* table = reinterpret_cast<std::uintptr_t*>(vtbl.address());
                if (!table) {
                    REX::ERROR("[BostonLean] %s V[%zu]: vtable address null", name, VIndex);
                    return false;
                }

                // Slot 1 in the BSTEventSink subobject is ProcessEvent
                auto* slot = &table[1];

                FnPtr before = reinterpret_cast<FnPtr>(*slot);
                if (!before) {
                    REX::ERROR("[BostonLean] %s V[%zu]: slot1 is null", name, VIndex);
                    return false;
                }

                // Save original, patch with our thunk
                orig = before;
                REL::WriteSafeData(reinterpret_cast<std::uintptr_t>(slot),
                                reinterpret_cast<std::uintptr_t>(&thunk));
                FnPtr after = reinterpret_cast<FnPtr>(*slot);

                const bool changed = (after == &thunk);
                REX::INFO("[BostonLean] hook {} V[{}] slot1 before={} after={} changed={}",
					name, VIndex, (void*)before, (void*)after, changed);

                return changed;
            } catch (...) {
                REX::ERROR("[BostonLean] %s V[%zu]: exception during vtable patch", name, VIndex);
                return false;
            }
        }
    };

    inline void install_all()
    {
        bool any = false;

        any |= InputSinkHook<RE::PlayerControls, 0>::install("RE::PlayerControls");
        any |= InputSinkHook<RE::PlayerControls, 1>::install("RE::PlayerControls");  // secondary table, if present
        any |= InputSinkHook<RE::MenuControls,   0>::install("RE::MenuControls");


        if (!any) {
            REX::ERROR("[BostonLean] No InputEvent sink hook installed. "
                       "Search your headers for classes deriving BSTEventSink<RE::InputEvent*> with VTABLE symbols and add them here.");
        }
    }
}

#pragma endregion

#pragma region Updates

// Try to resolve the first-person camera node
void LeanInputSink::resolve_cam_node()
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

RE::BSEventNotifyControl LeanInputSink::ProcessEvent(
	RE::InputEvent* const& a_event,
	RE::BSTEventSource<RE::InputEvent*>*
)
{
	float axis = _leanAxis;

	for (auto* e = a_event; e; e = e->next) {
		REX::INFO("Processing inptut event...");
		if (e->eventType != RE::INPUT_EVENT_TYPE::kButton) {
			continue;
		}

		auto* b = e->As<RE::ButtonEvent>();
		if (!b) {
			continue;
		}

		const auto code = b->GetBSButtonCode();
		const bool isPressed = b->QPressed();
		const bool justPressed = b->QJustPressed();
		const bool released = b->QReleased();

		REX::INFO("Processing button press...");

		if (code == RE::BS_BUTTON_CODE::kQ) { // Left
			if (justPressed || isPressed) axis = -1.0f;
			if (released) axis = 0.0f;
		} else if (code == RE::BS_BUTTON_CODE::kE) { // Right
			if (justPressed || isPressed) axis = +1.0f;
			if (released) axis = 0.0f;
		}

		// TODO gamepad support...
	}

	_leanAxis = std::clamp(axis, -1.0f, 1.0f);
	Update();

	return RE::BSEventNotifyControl::kContinue;
}

void LeanInputSink::Update()
{
	auto *pc = RE::PlayerCharacter::GetSingleton();
	auto *pcam = RE::PlayerCamera::GetSingleton();
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
	if (isAiming1st) {
		REX::INFO("Player is aiming...");
	} else {
		REX::INFO("no aiming...");
	}

	const float target = isAiming1st ? _leanAxis : 0.0f;

	const float dt = 1.0f / 60.0f; // Assume 60FPS
	const float smooth = std::clamp(dt * 15.0f, 0.0f, 1.0f);
	_current += (target - _current) * smooth;

	// Transform cam1st
	constexpr float kMaxOffset = 7.5f; // cm
	constexpr float kMaxRoll = 6.0f;   // deg

	auto& lt = _cam1st->local;
	lt.translate = RE::NiPoint3{};
	lt.rotate = RE::NiMatrix3();

	if (std::abs(_current) > 1e-3f) {
		const float offset = kMaxOffset * _current;
		const float rollRad = (kMaxRoll  * -_current) * (MATH_PI / 180.0f);

		lt.translate.x += offset;

		RE::NiMatrix3 rot;
		rot.FromEulerAnglesXYZ(0.0f, 0.0f, rollRad);
		lt.rotate = rot;
	}

	RE::NiUpdateData updateData{};
	_cam1st->Update(updateData);
}

#pragma endregion

#pragma region Initializers

void InitPlugin()
{
	if (auto console = RE::ConsoleLog::GetSingleton()) {
		console->PrintLine("Boston Lean v0.0.1 - Player combat leaning mechanics");
		console->PrintLine("By Chris Rowles.");
	}

	REX::INFO("plugin initialized.");
}

#pragma endregion

// extern "C" __declspec(dllexport) bool F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	REL::Trampoline& trampoline = REL::GetTrampoline();
	trampoline.create(static_cast<size_t>(64) * 1024);

	if (const auto messaging = F4SE::GetMessagingInterface()) {
		messaging->RegisterListener([](F4SE::MessagingInterface::Message* msg) {
			switch (msg->type)
			{
				case F4SE::MessagingInterface::kGameDataReady:
					REX::INFO("kGameDataReady event logged");
					InitPlugin();
					Hooks::install_all();
					break;
				case F4SE::MessagingInterface::kPostLoad:
					REX::INFO("kPostLoad event logged");
					break;
			}
		});
	}

	return true;
}
