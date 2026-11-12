// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Joseph R. Quinn

/*
 * Connection editor for OpenVPN SSO connections in GNOME Settings (GTK 4)
 * and nm-connection-editor (GTK 3). This file is built once against each
 * GTK version; nm-openvpn-sso-editor-plugin.c loads the matching library.
 *
 * Only the keys shown here are written. All other vpn.data items, such as
 * those set with nmcli, are left as they are.
 */

/* GtkComboBoxText and GtkFileChooserNative are deprecated in GTK 4.10 but
 * are the only APIs available in both GTK 3 and GTK 4. */
#define GDK_DISABLE_DEPRECATION_WARNINGS

#include <NetworkManager.h>
#include <errno.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <stdlib.h>

#define OPENVPN_SSO_SERVICE "org.freedesktop.NetworkManager.openvpn-sso"

/* vpn.data keys, read by src/config.rs */
#define KEY_CONFIG "config"
#define KEY_USERNAME "username"
#define KEY_REMOTE "remote"
#define KEY_PORT "port"
#define KEY_PROTO "proto"
#define KEY_CA "ca"

#define OPENVPN_SSO_TYPE_EDITOR (openvpn_sso_editor_get_type())
G_DECLARE_FINAL_TYPE(OpenvpnSsoEditor, openvpn_sso_editor, OPENVPN_SSO, EDITOR, GObject)

struct _OpenvpnSsoEditor {
    GObject parent;

    GtkWidget *widget;
    GtkWidget *username;
    GtkWidget *config;
    GtkWidget *remote;
    GtkWidget *port;
    GtkWidget *proto;

    GtkFileChooserNative *chooser;
};

static void openvpn_sso_editor_interface_init(NMVpnEditorInterface *iface);

G_DEFINE_TYPE_WITH_CODE(OpenvpnSsoEditor,
                        openvpn_sso_editor,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR, openvpn_sso_editor_interface_init))

/* -- GTK 3 / GTK 4 differences -------------------------------------------- */

static const char *
entry_get_text(GtkWidget *entry)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    return gtk_editable_get_text(GTK_EDITABLE(entry));
#else
    return gtk_entry_get_text(GTK_ENTRY(entry));
#endif
}

static void
entry_set_text(GtkWidget *entry, const char *text)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_editable_set_text(GTK_EDITABLE(entry), text ? text : "");
#else
    gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
#endif
}

static void
label_set_wrap(GtkWidget *label)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
#else
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
#endif
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
}

static void
widget_add_css_class(GtkWidget *widget, const char *css_class)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_widget_add_css_class(widget, css_class);
#else
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), css_class);
#endif
}

static GtkWindow *
toplevel_window(GtkWidget *widget)
{
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkRoot *root = gtk_widget_get_root(widget);

    return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
#else
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);

    return GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
#endif
}

/* -- Helpers ------------------------------------------------------------------ */

/* Text of an entry with surrounding whitespace removed; NULL when empty. */
static char *
entry_value(GtkWidget *entry)
{
    char *value = g_strstrip(g_strdup(entry_get_text(entry)));

    if (!*value) {
        g_free(value);
        return NULL;
    }
    return value;
}

static void
set_or_remove(NMSettingVpn *s_vpn, const char *key, const char *value)
{
    if (value && *value)
        nm_setting_vpn_add_data_item(s_vpn, key, value);
    else
        nm_setting_vpn_remove_data_item(s_vpn, key);
}

static gboolean
parse_port(const char *text, guint *port)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || *end || value < 1 || value > 65535)
        return FALSE;
    *port = (guint) value;
    return TRUE;
}

static void
emit_changed(OpenvpnSsoEditor *self)
{
    g_signal_emit_by_name(self, "changed");
}

static void
on_widget_changed(GtkWidget *widget, OpenvpnSsoEditor *self)
{
    emit_changed(self);
}

static void
on_chooser_response(GtkNativeDialog *dialog, int response, OpenvpnSsoEditor *self)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        g_autofree char *path = file ? g_file_get_path(file) : NULL;

        if (path)
            entry_set_text(self->config, path);
    }
    g_clear_object(&self->chooser);
}

static void
on_browse_clicked(GtkButton *button, OpenvpnSsoEditor *self)
{
    GtkFileFilter *filter;
    const char *current;

    if (self->chooser)
        return;

    self->chooser = gtk_file_chooser_native_new("Choose an OpenVPN configuration file",
                                                toplevel_window(GTK_WIDGET(button)),
                                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                                "_Open",
                                                "_Cancel");
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(self->chooser), TRUE);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "OpenVPN configuration (*.ovpn, *.conf)");
    gtk_file_filter_add_pattern(filter, "*.ovpn");
    gtk_file_filter_add_pattern(filter, "*.conf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(self->chooser), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(self->chooser), filter);

    current = entry_get_text(self->config);
    if (current && *current) {
        g_autoptr(GFile) file = g_file_new_for_path(current);

        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(self->chooser), file, NULL);
    }

    g_signal_connect(self->chooser, "response", G_CALLBACK(on_chooser_response), self);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(self->chooser));
}

/* -- Widget ------------------------------------------------------------------- */

static GtkWidget *
add_row(GtkGrid *grid, int row, const char *label_text, GtkWidget *field, const char *hint)
{
    GtkWidget *label = gtk_label_new_with_mnemonic(label_text);

    gtk_label_set_xalign(GTK_LABEL(label), 1.0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), field);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    gtk_grid_attach(grid, field, 1, row, 1, 1);

    if (hint) {
        GtkWidget *hint_label = gtk_label_new(hint);

        label_set_wrap(hint_label);
        widget_add_css_class(hint_label, "dim-label");
        gtk_grid_attach(grid, hint_label, 1, row + 1, 1, 1);
    }
    return label;
}

static GtkWidget *
new_entry(const char *placeholder)
{
    GtkWidget *entry = gtk_entry_new();

    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    return entry;
}

static void
build_widget(OpenvpnSsoEditor *self)
{
    GtkGrid *grid;
    GtkWidget *intro;
    GtkWidget *config_box;
    GtkWidget *browse;
    GtkWidget *config_label;

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);
    gtk_widget_set_margin_start(GTK_WIDGET(grid), 12);
    gtk_widget_set_margin_end(GTK_WIDGET(grid), 12);
    gtk_widget_set_margin_top(GTK_WIDGET(grid), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(grid), 12);

    intro = gtk_label_new("This VPN uses browser-based SSO authentication. "
                          "Your browser opens to log in when you connect; "
                          "no password is needed here.");
    label_set_wrap(intro);
    gtk_grid_attach(grid, intro, 0, 0, 2, 1);

    self->username = new_entry("Your VPN account name");
    add_row(grid, 1, "_Username", self->username,
            "Some servers end the session about an hour after connecting unless "
            "the client sends your real account name. Leave empty if your "
            "server does not need it.");

    self->config = new_entry("/path/to/config.ovpn");
    browse = gtk_button_new_with_mnemonic("_Browse…");
    config_box = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(config_box), 6);
    gtk_widget_set_hexpand(self->config, TRUE);
    gtk_grid_attach(GTK_GRID(config_box), self->config, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(config_box), browse, 1, 0, 1, 1);
    config_label = add_row(grid, 3, "_Configuration file", config_box, NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(config_label), self->config);

    self->remote = new_entry("Use server from configuration file");
    add_row(grid, 4, "_Gateway (optional)", self->remote, NULL);

    self->port = new_entry("Automatic");
    gtk_entry_set_input_purpose(GTK_ENTRY(self->port), GTK_INPUT_PURPOSE_DIGITS);
    add_row(grid, 5, "_Port (optional)", self->port, NULL);

    self->proto = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->proto), "", "Automatic");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->proto), "udp", "UDP");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(self->proto), "tcp", "TCP");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->proto), "");
    add_row(grid, 6, "P_rotocol", self->proto, NULL);

    /* Names let tests (test-editor.c) find the fields. */
    gtk_widget_set_name(self->username, "username");
    gtk_widget_set_name(self->config, "config");
    gtk_widget_set_name(browse, "browse");
    gtk_widget_set_name(self->remote, "remote");
    gtk_widget_set_name(self->port, "port");
    gtk_widget_set_name(self->proto, "proto");

    g_signal_connect(self->username, "changed", G_CALLBACK(on_widget_changed), self);
    g_signal_connect(self->config, "changed", G_CALLBACK(on_widget_changed), self);
    g_signal_connect(self->remote, "changed", G_CALLBACK(on_widget_changed), self);
    g_signal_connect(self->port, "changed", G_CALLBACK(on_widget_changed), self);
    g_signal_connect(self->proto, "changed", G_CALLBACK(on_widget_changed), self);
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse_clicked), self);

#if !GTK_CHECK_VERSION(4, 0, 0)
    gtk_widget_show_all(GTK_WIDGET(grid));
#endif

    self->widget = g_object_ref_sink(GTK_WIDGET(grid));
}

static void
load_connection(OpenvpnSsoEditor *self, NMConnection *connection)
{
    NMSettingVpn *s_vpn = connection ? nm_connection_get_setting_vpn(connection) : NULL;
    const char *proto;

    if (!s_vpn)
        return;

    entry_set_text(self->username, nm_setting_vpn_get_data_item(s_vpn, KEY_USERNAME));
    entry_set_text(self->config, nm_setting_vpn_get_data_item(s_vpn, KEY_CONFIG));
    entry_set_text(self->remote, nm_setting_vpn_get_data_item(s_vpn, KEY_REMOTE));
    entry_set_text(self->port, nm_setting_vpn_get_data_item(s_vpn, KEY_PORT));

    proto = nm_setting_vpn_get_data_item(s_vpn, KEY_PROTO);
    if (!proto || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->proto), proto))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->proto), "");
}

/* -- NMVpnEditor -------------------------------------------------------------- */

static GObject *
get_widget(NMVpnEditor *editor)
{
    return G_OBJECT(OPENVPN_SSO_EDITOR(editor)->widget);
}

static gboolean
update_connection(NMVpnEditor *editor, NMConnection *connection, GError **error)
{
    OpenvpnSsoEditor *self = OPENVPN_SSO_EDITOR(editor);
    NMSettingVpn *s_vpn;
    g_autofree char *username = entry_value(self->username);
    g_autofree char *config = entry_value(self->config);
    g_autofree char *remote = entry_value(self->remote);
    g_autofree char *port = entry_value(self->port);
    const char *proto = gtk_combo_box_get_active_id(GTK_COMBO_BOX(self->proto));
    guint port_number;

    s_vpn = nm_connection_get_setting_vpn(connection);

    /* Connections set up without a config file carry the CA and other
     * options as separate vpn.data items instead. */
    if (!config && !(s_vpn && nm_setting_vpn_get_data_item(s_vpn, KEY_CA))) {
        g_set_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_PROPERTY,
                    "Choose an OpenVPN configuration file");
        return FALSE;
    }
    if (config && !g_file_test(config, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    "The configuration file %s does not exist", config);
        return FALSE;
    }
    if (port && !parse_port(port, &port_number)) {
        g_set_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
                    "The port must be a number from 1 to 65535");
        return FALSE;
    }

    if (!s_vpn) {
        s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        nm_connection_add_setting(connection, NM_SETTING(s_vpn));
    }
    g_object_set(s_vpn, NM_SETTING_VPN_SERVICE_TYPE, OPENVPN_SSO_SERVICE, NULL);

    set_or_remove(s_vpn, KEY_USERNAME, username);
    set_or_remove(s_vpn, KEY_CONFIG, config);
    set_or_remove(s_vpn, KEY_REMOTE, remote);
    set_or_remove(s_vpn, KEY_PORT, port);
    set_or_remove(s_vpn, KEY_PROTO, proto);

    return TRUE;
}

static void
dispose(GObject *object)
{
    OpenvpnSsoEditor *self = OPENVPN_SSO_EDITOR(object);

    if (self->chooser) {
        g_signal_handlers_disconnect_by_data(self->chooser, self);
        gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(self->chooser));
        g_clear_object(&self->chooser);
    }
    if (self->widget) {
        g_signal_handlers_disconnect_by_data(self->username, self);
        g_signal_handlers_disconnect_by_data(self->config, self);
        g_signal_handlers_disconnect_by_data(self->remote, self);
        g_signal_handlers_disconnect_by_data(self->port, self);
        g_signal_handlers_disconnect_by_data(self->proto, self);
    }
    g_clear_object(&self->widget);

    G_OBJECT_CLASS(openvpn_sso_editor_parent_class)->dispose(object);
}

static void
openvpn_sso_editor_init(OpenvpnSsoEditor *self)
{
}

static void
openvpn_sso_editor_class_init(OpenvpnSsoEditorClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = dispose;
}

static void
openvpn_sso_editor_interface_init(NMVpnEditorInterface *iface)
{
    iface->get_widget = get_widget;
    iface->update_connection = update_connection;
}

G_MODULE_EXPORT NMVpnEditor *
nm_vpn_editor_factory_openvpn_sso(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
    OpenvpnSsoEditor *self;

    g_return_val_if_fail(!error || !*error, NULL);

    self = g_object_new(OPENVPN_SSO_TYPE_EDITOR, NULL);
    build_widget(self);
    load_connection(self, connection);
    return NM_VPN_EDITOR(self);
}
