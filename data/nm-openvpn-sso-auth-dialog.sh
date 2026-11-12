#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Joseph R. Quinn
#
# VPN auth dialog for OpenVPN SSO connections (auth-dialog= in
# nm-openvpn-sso-service.name).
#
# Secret agents such as GNOME Shell run the auth dialog whenever secrets of
# the connection are requested, for example when GNOME Settings opens the
# connection editor. These connections have no secrets: the login happens in
# the browser when connecting. Without an auth dialog GNOME Shell never
# answers the request, and GNOME Settings shows an empty editor after a
# 25-second timeout.
#
# In external UI mode (supports-external-ui-mode=true) no output means "no
# secrets to ask for". The agent writes the connection's data items and
# secrets to stdin, terminated by "DONE"; read them all first so the agent
# doesn't fail writing to a closed pipe.

while IFS= read -r line; do
    [ "$line" = "DONE" ] && break
done

exit 0
