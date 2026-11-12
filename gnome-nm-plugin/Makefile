# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Joseph R. Quinn
#
# GNOME connection editor for OpenVPN SSO connections.
#
#   make                 build all three libraries
#   make install         install them into $(DESTDIR)$(PLUGINDIR)
#   make check           test the editor with GTK 3 and GTK 4 (needs a display)
#
# PLUGINDIR must match the absolute plugin= path in
# data/nm-openvpn-sso-service.name; the editor libraries are loaded from the
# same directory as the plugin.

PKG_CONFIG ?= pkg-config
CC ?= cc
CFLAGS ?= -O2 -g
PLUGINDIR ?= /usr/lib/NetworkManager

WARNINGS = -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration
COMMON_CFLAGS = -fPIC -fvisibility=hidden $(WARNINGS)
COMMON_LDFLAGS = -shared -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now

LIBNM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libnm gmodule-2.0)
LIBNM_LIBS := $(shell $(PKG_CONFIG) --libs libnm gmodule-2.0)

PLUGIN = libnm-vpn-plugin-openvpn-sso.so
EDITOR_GTK3 = libnm-vpn-plugin-openvpn-sso-editor.so
EDITOR_GTK4 = libnm-gtk4-vpn-plugin-openvpn-sso-editor.so

BUILDDIR ?= build

all: $(BUILDDIR)/$(PLUGIN) $(BUILDDIR)/$(EDITOR_GTK3) $(BUILDDIR)/$(EDITOR_GTK4)

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/$(PLUGIN): nm-openvpn-sso-editor-plugin.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(COMMON_CFLAGS) $(LIBNM_CFLAGS) $(LDFLAGS) $(COMMON_LDFLAGS) \
		-o $@ $< $(LIBNM_LIBS) -ldl

$(BUILDDIR)/$(EDITOR_GTK3): nm-openvpn-sso-editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(COMMON_CFLAGS) $(LIBNM_CFLAGS) $(shell $(PKG_CONFIG) --cflags gtk+-3.0) \
		$(LDFLAGS) $(COMMON_LDFLAGS) -o $@ $< $(LIBNM_LIBS) $(shell $(PKG_CONFIG) --libs gtk+-3.0)

$(BUILDDIR)/$(EDITOR_GTK4): nm-openvpn-sso-editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(COMMON_CFLAGS) $(LIBNM_CFLAGS) $(shell $(PKG_CONFIG) --cflags gtk4) \
		$(LDFLAGS) $(COMMON_LDFLAGS) -o $@ $< $(LIBNM_LIBS) $(shell $(PKG_CONFIG) --libs gtk4)

$(BUILDDIR)/test-editor-gtk3: test-editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(WARNINGS) $(LIBNM_CFLAGS) $(shell $(PKG_CONFIG) --cflags gtk+-3.0) \
		$(LDFLAGS) -o $@ $< $(LIBNM_LIBS) $(shell $(PKG_CONFIG) --libs gtk+-3.0)

$(BUILDDIR)/test-editor-gtk4: test-editor.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(WARNINGS) $(LIBNM_CFLAGS) $(shell $(PKG_CONFIG) --cflags gtk4) \
		$(LDFLAGS) -o $@ $< $(LIBNM_LIBS) $(shell $(PKG_CONFIG) --libs gtk4)

check: all $(BUILDDIR)/test-editor-gtk3 $(BUILDDIR)/test-editor-gtk4
	$(BUILDDIR)/test-editor-gtk3 $(abspath $(BUILDDIR)/$(PLUGIN))
	$(BUILDDIR)/test-editor-gtk4 $(abspath $(BUILDDIR)/$(PLUGIN))

install: all
	install -Dm755 $(BUILDDIR)/$(PLUGIN) $(DESTDIR)$(PLUGINDIR)/$(PLUGIN)
	install -Dm755 $(BUILDDIR)/$(EDITOR_GTK3) $(DESTDIR)$(PLUGINDIR)/$(EDITOR_GTK3)
	install -Dm755 $(BUILDDIR)/$(EDITOR_GTK4) $(DESTDIR)$(PLUGINDIR)/$(EDITOR_GTK4)

uninstall:
	rm -f $(DESTDIR)$(PLUGINDIR)/$(PLUGIN) $(DESTDIR)$(PLUGINDIR)/$(EDITOR_GTK3) \
		$(DESTDIR)$(PLUGINDIR)/$(EDITOR_GTK4)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all check install uninstall clean
