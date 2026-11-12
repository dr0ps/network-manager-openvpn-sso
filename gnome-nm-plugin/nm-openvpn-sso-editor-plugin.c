// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Joseph R. Quinn

/*
 * NetworkManager VPN editor plugin for OpenVPN SSO connections.
 *
 * GNOME Settings and nm-connection-editor load this library through the
 * [libnm] plugin= entry of nm-openvpn-sso-service.name. It has no GTK
 * dependency itself: GNOME Settings uses GTK 4 and nm-connection-editor
 * GTK 3, and both versions cannot live in one process, so the editor widget
 * comes from a separate library built for each, loaded from the directory
 * this library is installed in.
 */

#define _GNU_SOURCE
#include <dlfcn.h>

#include <NetworkManager.h>
#include <gmodule.h>

#define OPENVPN_SSO_SERVICE "org.freedesktop.NetworkManager.openvpn-sso"

#define EDITOR_LIB_GTK3 "libnm-vpn-plugin-openvpn-sso-editor.so"
#define EDITOR_LIB_GTK4 "libnm-gtk4-vpn-plugin-openvpn-sso-editor.so"
#define EDITOR_FACTORY "nm_vpn_editor_factory_openvpn_sso"

typedef NMVpnEditor *(*EditorFactory)(NMVpnEditorPlugin *plugin,
                                      NMConnection *connection,
                                      GError **error);

enum {
    PROP_0,
    PROP_NAME,
    PROP_DESC,
    PROP_SERVICE,
};

#define OPENVPN_SSO_TYPE_EDITOR_PLUGIN (openvpn_sso_editor_plugin_get_type())
G_DECLARE_FINAL_TYPE(OpenvpnSsoEditorPlugin, openvpn_sso_editor_plugin, OPENVPN_SSO, EDITOR_PLUGIN, GObject)

struct _OpenvpnSsoEditorPlugin {
    GObject parent;
};

static void openvpn_sso_editor_plugin_interface_init(NMVpnEditorPluginInterface *iface);

G_DEFINE_TYPE_WITH_CODE(OpenvpnSsoEditorPlugin,
                        openvpn_sso_editor_plugin,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN,
                                              openvpn_sso_editor_plugin_interface_init))

/* Directory this library was loaded from; the editor libraries sit next to it. */
static char *
plugin_dir(void)
{
    Dl_info info;

    if (dladdr((void *) plugin_dir, &info) == 0 || !info.dli_fname)
        return NULL;
    return g_path_get_dirname(info.dli_fname);
}

static EditorFactory
load_editor_factory(GError **error)
{
    /* Loaded once and kept: GTK types registered by the editor library
     * cannot be unregistered, so the library must never be unloaded. */
    static EditorFactory factory;
    g_autofree char *dir = NULL;
    g_autofree char *path = NULL;
    const char *lib_name;
    void *handle;

    if (factory)
        return factory;

    /* GTK 3 and GTK 4 each export symbols the other lacks. */
    if (dlsym(RTLD_DEFAULT, "gtk_container_add"))
        lib_name = EDITOR_LIB_GTK3;
    else if (dlsym(RTLD_DEFAULT, "gtk_box_append"))
        lib_name = EDITOR_LIB_GTK4;
    else {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
                    "Neither GTK 3 nor GTK 4 is loaded; cannot create the OpenVPN SSO editor");
        return NULL;
    }

    dir = plugin_dir();
    if (!dir) {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
                    "Cannot determine where the OpenVPN SSO plugin is installed");
        return NULL;
    }
    path = g_build_filename(dir, lib_name, NULL);

    handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
                    "Cannot load the OpenVPN SSO editor %s: %s", path, dlerror());
        return NULL;
    }

    factory = (EditorFactory) dlsym(handle, EDITOR_FACTORY);
    if (!factory) {
        g_set_error(error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
                    "The OpenVPN SSO editor %s lacks %s", path, EDITOR_FACTORY);
        dlclose(handle);
        return NULL;
    }
    return factory;
}

static NMVpnEditor *
get_editor(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
    EditorFactory factory = load_editor_factory(error);

    if (!factory)
        return NULL;
    return factory(plugin, connection, error);
}

static NMVpnEditorPluginCapability
get_capabilities(NMVpnEditorPlugin *plugin)
{
    /* No import/export: .ovpn import belongs to the regular OpenVPN plugin;
     * an SSO connection is created by switching an imported connection's
     * service type or from the "Add VPN" dialog. */
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE;
}

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string(value, "OpenVPN SSO");
        break;
    case PROP_DESC:
        g_value_set_string(value, "OpenVPN with browser-based single sign-on (OAuth 2.0 / OIDC)");
        break;
    case PROP_SERVICE:
        g_value_set_string(value, OPENVPN_SSO_SERVICE);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
openvpn_sso_editor_plugin_init(OpenvpnSsoEditorPlugin *plugin)
{
}

static void
openvpn_sso_editor_plugin_class_init(OpenvpnSsoEditorPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = get_property;

    g_object_class_override_property(object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property(object_class, PROP_DESC, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property(object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

static void
openvpn_sso_editor_plugin_interface_init(NMVpnEditorPluginInterface *iface)
{
    iface->get_editor = get_editor;
    iface->get_capabilities = get_capabilities;
}

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory(GError **error)
{
    g_return_val_if_fail(!error || !*error, NULL);

    return g_object_new(OPENVPN_SSO_TYPE_EDITOR_PLUGIN, NULL);
}
