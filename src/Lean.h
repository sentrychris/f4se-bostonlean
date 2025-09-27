#pragma once

class LeanInputSink final : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    static LeanInputSink& Get() { static LeanInputSink i; return i; }

    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const& a_event,
        RE::BSTEventSource<RE::InputEvent*>* a_source
    ) override;

    void Update();

private:
    LeanInputSink() = default;
    void resolve_cam_node();

    float _leanAxis = 0.0f;
    float _current = 0.0f;
    RE::NiAVObject* _cam1st = nullptr;
    RE::NiPointer<RE::NiAVObject> _fpRoot;
};
