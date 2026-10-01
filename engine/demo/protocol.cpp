#include "demo/protocol.h"

#include <array>

namespace gmdr::demo {

const char* netMsgName(int type) {
    switch (static_cast<NetMsg>(type)) {
    case NetMsg::Nop: return "net_NOP";
    case NetMsg::Disconnect: return "net_Disconnect";
    case NetMsg::File: return "net_File";
    case NetMsg::Tick: return "net_Tick";
    case NetMsg::StringCmd: return "net_StringCmd";
    case NetMsg::SetConVar: return "net_SetConVar";
    case NetMsg::SignonState: return "net_SignonState";
    case NetMsg::Print: return "svc_Print";
    case NetMsg::ServerInfo: return "svc_ServerInfo";
    case NetMsg::SendTable: return "svc_SendTable";
    case NetMsg::ClassInfo: return "svc_ClassInfo";
    case NetMsg::SetPause: return "svc_SetPause";
    case NetMsg::CreateStringTable: return "svc_CreateStringTable";
    case NetMsg::UpdateStringTable: return "svc_UpdateStringTable";
    case NetMsg::VoiceInit: return "svc_VoiceInit";
    case NetMsg::VoiceData: return "svc_VoiceData";
    case NetMsg::Sounds: return "svc_Sounds";
    case NetMsg::SetView: return "svc_SetView";
    case NetMsg::FixAngle: return "svc_FixAngle";
    case NetMsg::CrosshairAngle: return "svc_CrosshairAngle";
    case NetMsg::BSPDecal: return "svc_BSPDecal";
    case NetMsg::UserMessage: return "svc_UserMessage";
    case NetMsg::EntityMessage: return "svc_EntityMessage";
    case NetMsg::GameEvent: return "svc_GameEvent";
    case NetMsg::PacketEntities: return "svc_PacketEntities";
    case NetMsg::TempEntities: return "svc_TempEntities";
    case NetMsg::Prefetch: return "svc_Prefetch";
    case NetMsg::Menu: return "svc_Menu";
    case NetMsg::GameEventList: return "svc_GameEventList";
    case NetMsg::GetCvarValue: return "svc_GetCvarValue";
    case NetMsg::CmdKeyValues: return "svc_CmdKeyValues";
    case NetMsg::GModServerToClient: return "svc_GMod_ServerToClient";
    }
    return "unknown";
}

std::span<const ProtocolVariant> knownProtocolVariants() {
    static const std::array<ProtocolVariant, 1> variants = {ProtocolVariant{.name = "gmod-2025"}};
    return variants;
}

} // namespace gmdr::demo
