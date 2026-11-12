// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Joseph R. Quinn

/*
 * Loads the built plugin the way libnm does for GNOME Settings and
 * nm-connection-editor, and checks how the editor reads and writes
 * connections. Built once against GTK 3 and once against GTK 4; needs a
 * display (run under xvfb-run in CI).
 *
 *   test-editor-gtkN <path to libnm-vpn-plugin-openvpn-sso.so>
 */

#define GDK_DISABLE_DEPRECATION_WARNINGS

#include <NetworkManager.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <unistd.h>

#define SERVICE "org.freedesktop.NetworkManager.openvpn-sso"

static const char *plugin_path;
static char *config_file;

static GtkWidget *
find_named(GtkWidget *widget, const char *name)
{
    if (g_strcmp0(gtk_widget_get_name(widget), name) == 0)
        return widget;
#if GTK_CHECK_VERSION(4, 0, 0)
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_named(child, name);

        if (found)
            return found;
    }
#else
    if (GTK_IS_CONTAINER(widget)) {
        g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(widget));

        for (GList *l = children; l; l = l->next) {
            GtkWidget *found = find_named(l->data, name);

            if (found)
                return found;
        }
    }
#endif
    return NULL;
}

static GtkWidget *
field(NMVpnEditor *editor, const char *name)
{
    GtkWidget *widget = find_named(GTK_WIDGET(nm_vpn_editor_get_widget(editor)), name);

    g_assert_nonnull(widget);
    return widget;
}

static const char *
get_text(GtkWidget *entry)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    return gtk_editable_get_text(GTK_EDITABLE(entry));
#else
    return gtk_entry_get_text(GTK_ENTRY(entry));
#endif
}

static void
set_text(GtkWidget *entry, const char *text)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_editable_set_text(GTK_EDITABLE(entry), text);
#else
    gtk_entry_set_text(GTK_ENTRY(entry), text);
#endif
}

static NMVpnEditorPlugin *
load_plugin(void)
{
    g_autoptr(GError) error = NULL;
    NMVpnEditorPlugin *plugin;

    /* check_owner -1: the build directory is owned by the user, not root. */
    plugin = nm_vpn_editor_plugin_load_from_file(plugin_path, SERVICE, -1, NULL, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(plugin);
    return plugin;
}

static NMConnection *
vpn_connection(const char *const *items)
{
    NMConnection *connection = nm_simple_connection_new();
    NMSettingConnection *s_con = NM_SETTING_CONNECTION(nm_setting_connection_new());
    NMSettingVpn *s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());

    g_object_set(s_con,
                 NM_SETTING_CONNECTION_ID, "test",
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
                 NULL);
    nm_connection_add_setting(connection, NM_SETTING(s_con));
    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, SERVICE, NULL);
    for (; items && items[0]; items += 2)
        nm_setting_vpn_add_data_item(s_vpn, items[0], items[1]);
    nm_connection_add_setting(connection, NM_SETTING(s_vpn));
    return connection;
}

static NMVpnEditor *
editor_for(NMVpnEditorPlugin *plugin, NMConnection *connection)
{
    g_autoptr(GError) error = NULL;
    NMVpnEditor *editor = nm_vpn_editor_plugin_get_editor(plugin, connection, &error);

    g_assert_no_error(error);
    g_assert_nonnull(editor);
    g_assert_true(GTK_IS_WIDGET(nm_vpn_editor_get_widget(editor)));
    return editor;
}

static const char *
data_item(NMConnection *connection, const char *key)
{
    return nm_setting_vpn_get_data_item(nm_connection_get_setting_vpn(connection), key);
}

static void
test_plugin_properties(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    g_autofree char *service = NULL;
    g_autofree char *name = NULL;

    g_object_get(plugin,
                 NM_VPN_EDITOR_PLUGIN_SERVICE, &service,
                 NM_VPN_EDITOR_PLUGIN_NAME, &name,
                 NULL);
    g_assert_cmpstr(service, ==, SERVICE);
    g_assert_cmpstr(name, ==, "OpenVPN SSO");
    g_assert_cmpint(nm_vpn_editor_plugin_get_capabilities(plugin), ==,
                    NM_VPN_EDITOR_PLUGIN_CAPABILITY_NONE);
}

static void
test_loads_existing_values(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"config", config_file, "username", "ebert", "remote", "vpn.example.com",
                           "port", "1194", "proto", "tcp", NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);

    g_assert_cmpstr(get_text(field(editor, "username")), ==, "ebert");
    g_assert_cmpstr(get_text(field(editor, "config")), ==, config_file);
    g_assert_cmpstr(get_text(field(editor, "remote")), ==, "vpn.example.com");
    g_assert_cmpstr(get_text(field(editor, "port")), ==, "1194");
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(field(editor, "proto"))), ==, "tcp");
}

static void
test_save_keeps_other_settings(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"config", config_file, "username", "ebert", "ta-dir", "1", NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    g_autoptr(GError) error = NULL;

    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(data_item(connection, "username"), ==, "ebert");
    g_assert_cmpstr(data_item(connection, "config"), ==, config_file);
    g_assert_cmpstr(data_item(connection, "ta-dir"), ==, "1");
    g_assert_null(data_item(connection, "proto"));
    g_assert_null(data_item(connection, "port"));
}

static void
test_save_writes_edits(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"config", config_file, "username", "ebert", "remote", "old.example.com", NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    g_autoptr(GError) error = NULL;

    set_text(field(editor, "username"), "  someone  ");
    set_text(field(editor, "remote"), "");
    set_text(field(editor, "port"), "443");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(field(editor, "proto")), "udp");

    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(data_item(connection, "username"), ==, "someone");
    g_assert_null(data_item(connection, "remote"));
    g_assert_cmpstr(data_item(connection, "port"), ==, "443");
    g_assert_cmpstr(data_item(connection, "proto"), ==, "udp");
}

static void
on_changed(NMVpnEditor *editor, int *count)
{
    (*count)++;
}

static void
test_edits_emit_changed(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"config", config_file, NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    int count = 0;

    g_signal_connect(editor, "changed", G_CALLBACK(on_changed), &count);
    set_text(field(editor, "username"), "ebert");
    g_assert_cmpint(count, >, 0);
}

static void
test_rejects_invalid_input(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"config", config_file, NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    GError *error = NULL;

    set_text(field(editor, "port"), "70000");
    g_assert_false(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY);
    g_clear_error(&error);

    set_text(field(editor, "port"), "");
    set_text(field(editor, "config"), "/nonexistent/config.ovpn");
    g_assert_false(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY);
    g_clear_error(&error);

    set_text(field(editor, "config"), "");
    g_assert_false(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_PROPERTY);
    g_clear_error(&error);

    /* Rejected saves leave the connection untouched. */
    g_assert_cmpstr(data_item(connection, "config"), ==, config_file);
}

static void
test_accepts_ca_without_config_file(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    const char *items[] = {"ca", "/etc/openvpn/ca.crt", "remote", "vpn.example.com:1194", NULL};
    g_autoptr(NMConnection) connection = vpn_connection(items);
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    g_autoptr(GError) error = NULL;

    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    g_assert_null(data_item(connection, "config"));
    g_assert_cmpstr(data_item(connection, "ca"), ==, "/etc/openvpn/ca.crt");
}

static void
test_new_connection(void)
{
    g_autoptr(NMVpnEditorPlugin) plugin = load_plugin();
    g_autoptr(NMConnection) connection = nm_simple_connection_new();
    g_autoptr(NMVpnEditor) editor = editor_for(plugin, connection);
    g_autoptr(GError) error = NULL;

    set_text(field(editor, "config"), config_file);
    g_assert_true(nm_vpn_editor_update_connection(editor, connection, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(nm_setting_vpn_get_service_type(nm_connection_get_setting_vpn(connection)), ==,
                    SERVICE);
    g_assert_cmpstr(data_item(connection, "config"), ==, config_file);
}

int
main(int argc, char **argv)
{
    int fd;
    int result;

#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_init();
#else
    gtk_init(&argc, &argv);
#endif
    g_test_init(&argc, &argv, NULL);

    g_assert_cmpint(argc, ==, 2);
    plugin_path = argv[1];

    fd = g_file_open_tmp("openvpn-sso-test-XXXXXX.ovpn", &config_file, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    g_test_add_func("/editor/plugin-properties", test_plugin_properties);
    g_test_add_func("/editor/loads-existing-values", test_loads_existing_values);
    g_test_add_func("/editor/save-keeps-other-settings", test_save_keeps_other_settings);
    g_test_add_func("/editor/save-writes-edits", test_save_writes_edits);
    g_test_add_func("/editor/edits-emit-changed", test_edits_emit_changed);
    g_test_add_func("/editor/rejects-invalid-input", test_rejects_invalid_input);
    g_test_add_func("/editor/accepts-ca-without-config-file", test_accepts_ca_without_config_file);
    g_test_add_func("/editor/new-connection", test_new_connection);

    result = g_test_run();
    g_unlink(config_file);
    g_free(config_file);
    return result;
}
