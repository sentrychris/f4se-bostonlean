#pragma region Variables

// Relocated pointer to the target function in the game's memory that updates actor state
REL::Relocation<uintptr_t> ptr_RunActorUpdates{ REL::ID(556439), 0x17 };

// Storage location for the original function pointer overwritten by the trampoline hook
uintptr_t RunActorUpdatesOrig;

// Cached pointer to the game's PlayerCamera instance
RE::PlayerCamera* pcam;

// Cached pointer to the game's PlayerCharacter instance
RE::PlayerCharacter* p;

#pragma endregion

#pragma region Utils

bool IsInADS(RE::Actor* a)
{
	return ((int)a->gunState == 0x8 || (int)a->gunState == 0x6);
}

bool IsInPowerArmor(RE::Actor* a)
{
	if (!a->extraList) {
		return false;
	}

	return a->extraList->HasType(RE::EXTRA_DATA_TYPE::kPowerArmor);
}

bool IsFirstPerson()
{
	return pcam->currentState == pcam->cameraStates[RE::CameraState::kFirstPerson];
}

float Sign(float f)
{
	if (f == 0) {
		return 0;
	}

	return abs(f) / f;
}

#pragma endregion

#pragma region Events

void HookedActorUpdate(RE::ProcessLists* list, float dt, bool instant)
{
	REX::INFO("Successfully hooked native ActorUpdate function");

	typedef void (*FnUpdate)(RE::ProcessLists*, float, bool);
	FnUpdate fn = (FnUpdate)RunActorUpdatesOrig;
	if (fn) {
	    (*fn)(list, dt, instant);
	}

	RE::Actor* a = p;
	if (IsInADS(a)) {
		REX::INFO("Entered ADS state");
	}
}

#pragma endregion

#pragma region Initializers

void InitPlugin()
{
	p = RE::PlayerCharacter::GetSingleton();
	pcam = RE::PlayerCamera::GetSingleton();
	REX::INFO("plugin initialized.");
}

#pragma endregion

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	REX::INFO("Hello World!");

	REL::Trampoline& trampoline = REL::GetTrampoline();
	trampoline.create(static_cast<size_t>(64) * 1024);
	RunActorUpdatesOrig = trampoline.write_call<5>(ptr_RunActorUpdates.address(), &HookedActorUpdate);

	if (const auto messaging = F4SE::GetMessagingInterface()) {
		messaging->RegisterListener([](F4SE::MessagingInterface::Message* msg) {
			switch (msg->type)
			{
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
