#pragma once
#include <cstddef>
#include <cstdint>

// Minimal binary interface declarations, checked against the official SKSE PluginAPI.h.
// No CommonLib layouts or address-library IDs are assumed by this plugin.
namespace skse_abi {
struct Interface {
    std::uint32_t skse_version, runtime_version, editor_version, is_editor;
    void* (*query_interface)(std::uint32_t);
    std::uint32_t (*get_plugin_handle)();
    std::uint32_t (*get_release_index)();
    const void* (*get_plugin_info)(const char*);
};
struct Message {const char* sender; std::uint32_t type,data_length; void* data;};
struct Messaging {
    std::uint32_t version;
    bool (*register_listener)(std::uint32_t,const char*,void(*)(Message*));
    bool (*dispatch)(std::uint32_t,std::uint32_t,void*,std::uint32_t,const char*);
    void* (*get_dispatcher)(std::uint32_t);
};
struct VersionData {
    std::uint32_t data_version, plugin_version;
    char name[256],author[256],support_email[252];
    std::uint32_t independence_ex,independence,compatible[16],skse_required;
};
struct Trampoline {
    std::uint32_t version;
    void* (*allocate_branch)(std::uint32_t,std::size_t);
    void* (*allocate_local)(std::uint32_t,std::size_t);
};
static_assert(sizeof(Trampoline)==24 && offsetof(Trampoline,allocate_branch)==8);
static_assert(sizeof(Interface)==48 && offsetof(Interface,query_interface)==16);
static_assert(sizeof(Messaging)==32 && offsetof(Messaging,register_listener)==8);
static_assert(sizeof(Message)==24 && sizeof(VersionData)==848);
inline constexpr std::uint32_t messaging_id=5, data_loaded=8;
inline constexpr std::uint32_t trampoline_id=7;
}
