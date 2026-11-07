#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# Mac side of the router setup: copies the bundle to an OpenWrt router over SSH
# and runs setup.sh there. Usage: router-run.sh install|remove HOST [PORT] [USER]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ACTION=$1; HOST=$2; PORT=${3:-}; USER_=${4:-root}
export SSH_ASKPASS="$HERE/router-askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=${DISPLAY:-:0}
# StrictHostKeyChecking=ask: an unknown router key is shown for confirmation (askpass)
args=(-o ConnectTimeout=10 -o StrictHostKeyChecking=ask -o NumberOfPasswordPrompts=2)
[ -n "$PORT" ] && args+=(-p "$PORT")
# a host alias from ~/.ssh/config keeps its own user
[[ "$HOST" == *.* || "$HOST" == *:* ]] && HOST="$USER_@$HOST"
ssh "${args[@]}" "$HOST" 'rm -rf /tmp/sp211-setup && mkdir -p /tmp/sp211-setup && tar -xzf - -C /tmp/sp211-setup && sh /tmp/sp211-setup/setup.sh '"$ACTION"'; rc=$?; rm -rf /tmp/sp211-setup; exit $rc' \
  < "$HERE/sp211-airprint-router.tar.gz" 2>&1 | grep -E "^SP211:|Permission denied|Connection|Could not|refused|timed out|Host key" | tail -5
exit "${PIPESTATUS[0]}"
