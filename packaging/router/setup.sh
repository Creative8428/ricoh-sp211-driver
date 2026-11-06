#!/bin/sh
# Installs or removes the Ricoh SP 211 AirPrint server on OpenWrt.
# Usage: sh setup.sh install|remove|status    (run as root on the router,
# from the directory that contains sp211-airprintd and sp211-airprint.init)
set -e
ACTION=${1:-status}
HERE=$(cd "$(dirname "$0")" && pwd)
CONF=/etc/sp211-airprint
SVC=/etc/avahi/services/sp211-airprint.service
RULE=sp211_airprint_ipp
SVCUSER=sp211
HOTPLUG=/etc/hotplug.d/usbmisc/20-sp211-airprint

say() { echo "SP211: $*"; }

avahi_reload() {
	if [ -x /etc/init.d/avahi-daemon ]; then /etc/init.d/avahi-daemon reload >/dev/null 2>&1 || /etc/init.d/avahi-daemon restart >/dev/null 2>&1 || true; fi
}

write_service() {
	local uuid host name
	uuid=$(cat $CONF/uuid); name=$(cat $CONF/name)
	host=$(uci -q get system.@system[0].hostname || cat /proc/sys/kernel/hostname)
	mkdir -p /etc/avahi/services
	cat > $SVC <<XML
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name>$name</name>
  <service>
    <type>_ipp._tcp</type>
    <subtype>_universal._sub._ipp._tcp</subtype>
    <port>631</port>
    <txt-record>txtvers=1</txt-record>
    <txt-record>qtotal=1</txt-record>
    <txt-record>rp=ipp/print</txt-record>
    <txt-record>ty=RICOH SP 211</txt-record>
    <txt-record>product=(RICOH SP 211)</txt-record>
    <txt-record>usb_MFG=RICOH</txt-record>
    <txt-record>usb_MDL=SP 211</txt-record>
    <txt-record>pdl=image/urf,image/pwg-raster</txt-record>
    <txt-record>URF=V1.4,W8,RS600,CP1,DM1,IS1-20,MT1-2-3-4-5-6-8-9-10-11-12-13</txt-record>
    <txt-record>Color=F</txt-record>
    <txt-record>Duplex=T</txt-record>
    <txt-record>kind=document</txt-record>
    <txt-record>PaperMax=legal-A4</txt-record>
    <txt-record>priority=50</txt-record>
    <txt-record>note=</txt-record>
    <txt-record>UUID=$uuid</txt-record>
    <txt-record>adminurl=http://$host.local:631/</txt-record>
  </service>
  <service>
    <type>_printer._tcp</type>
    <port>0</port>
  </service>
</service-group>
XML
}

do_install() {
	[ -f "$HERE/sp211-airprintd" ] && [ -f "$HERE/sp211-usb-recover" ] && [ -f "$HERE/sp211-airprint.hotplug" ] || { say "incomplete bundle"; exit 1; }
	# USB printer class driver (/dev/usb/lp0)
	if [ ! -e /lib/modules/$(uname -r)/usblp.ko ]; then
		say "installing kmod-usb-printer"
		if command -v apk >/dev/null; then apk update >/dev/null && apk add kmod-usb-printer
		else opkg update >/dev/null && opkg install kmod-usb-printer; fi
		mkdir -p $CONF && touch $CONF/installed-kmod
	fi
	modprobe usblp 2>/dev/null || insmod usblp 2>/dev/null || true

	# unprivileged service user in the "lp" group
	. /lib/functions.sh
	group_exists lp || group_add lp 7
	lpgid=$(awk -F: '$1=="lp"{print $3}' /etc/group)
	if ! user_exists $SVCUSER; then
		uid=230; while grep -q "^[^:]*:[^:]*:$uid:" /etc/passwd; do uid=$((uid + 1)); done
		user_add $SVCUSER $uid $lpgid "sp211 AirPrint server" /var /bin/false
	fi
	mkdir -p /etc/hotplug.d/usbmisc
	cp "$HERE/sp211-airprint.hotplug" $HOTPLUG
	chmod 644 $HOTPLUG
	for d in /dev/usb/lp*; do [ -e "$d" ] && chgrp lp "$d" && chmod 660 "$d"; done

	mkdir -p $CONF
	# stable UUID derived from the router's MAC address: clients (macOS keeps
	# it in the printer URI) still find the printer after a reinstall
	if [ ! -s $CONF/uuid ] || [ "$UUID_RESET" = 1 ]; then
		mac=$(cat /sys/class/net/eth0/address 2>/dev/null || cat /sys/class/net/br-lan/address 2>/dev/null)
		if [ -n "$mac" ]; then
			h=$(echo "sp211-airprint-$mac" | md5sum | cut -c1-32)
			echo "$(echo $h | cut -c1-8)-$(echo $h | cut -c9-12)-3$(echo $h | cut -c14-16)-a$(echo $h | cut -c18-20)-$(echo $h | cut -c21-32)" > $CONF/uuid
		else
			cat /proc/sys/kernel/random/uuid > $CONF/uuid
		fi
	fi
	[ -s $CONF/name ] || echo "RICOH SP 211" > $CONF/name
	/etc/init.d/sp211-airprint stop >/dev/null 2>&1 || true
	cp "$HERE/sp211-airprintd" /usr/sbin/sp211-airprintd
	chmod 755 /usr/sbin/sp211-airprintd
	mkdir -p /usr/libexec
	cp "$HERE/sp211-usb-recover" /usr/libexec/sp211-usb-recover
	chmod 755 /usr/libexec/sp211-usb-recover
	cp "$HERE/sp211-airprint.init" /etc/init.d/sp211-airprint
	chmod 755 /etc/init.d/sp211-airprint

	# firewall: allow IPP (631/tcp) from the LAN zone only
	uci -q delete firewall.$RULE || true
	uci set firewall.$RULE=rule
	uci set firewall.$RULE.name='SP211-AirPrint-IPP'
	uci set firewall.$RULE.src='lan'
	uci set firewall.$RULE.proto='tcp'
	uci set firewall.$RULE.dest_port='631'
	uci set firewall.$RULE.target='ACCEPT'
	uci commit firewall
	/etc/init.d/firewall reload >/dev/null 2>&1 || true

	write_service
	avahi_reload

	/etc/init.d/sp211-airprint enable

	# keep across sysupgrade (including the autostart links in /etc/rc.d,
	# which are not part of the default backup)
	for f in /usr/sbin/sp211-airprintd /usr/libexec/sp211-usb-recover /etc/init.d/sp211-airprint $CONF/ $SVC $HOTPLUG \
	         /etc/rc.d/S95sp211-airprint /etc/rc.d/K10sp211-airprint; do
		grep -qxF "$f" /etc/sysupgrade.conf 2>/dev/null || echo "$f" >> /etc/sysupgrade.conf
	done

	/etc/init.d/sp211-airprint start
	sleep 1
	if pidof sp211-airprintd >/dev/null; then say "installed and running"; else say "installed but not running"; exit 1; fi
}

do_remove() {
	/etc/init.d/sp211-airprint stop >/dev/null 2>&1 || true
	/etc/init.d/sp211-airprint disable >/dev/null 2>&1 || true
	rm -f /etc/init.d/sp211-airprint /usr/sbin/sp211-airprintd /usr/libexec/sp211-usb-recover $SVC $HOTPLUG
	if grep -q "^$SVCUSER:" /etc/passwd; then
		sed -i "/^$SVCUSER:/d" /etc/passwd /etc/shadow 2>/dev/null
	fi
	rm -rf /tmp/sp211-spool
	avahi_reload
	if uci -q get firewall.$RULE >/dev/null; then
		uci delete firewall.$RULE; uci commit firewall
		/etc/init.d/firewall reload >/dev/null 2>&1 || true
	fi
	if [ -f $CONF/installed-kmod ]; then
		rmmod usblp 2>/dev/null || true
		if command -v apk >/dev/null; then apk del kmod-usb-printer >/dev/null 2>&1 || true
		else opkg remove kmod-usb-printer >/dev/null 2>&1 || true; fi
	fi
	rm -rf $CONF
	if [ -f /etc/sysupgrade.conf ]; then
		sed -i '/sp211-airprint/d' /etc/sysupgrade.conf
	fi
	say "removed"
}

do_status() {
	if pidof sp211-airprintd >/dev/null; then say "running"; else say "not running"; fi
	[ -e /dev/usb/lp0 ] && say "printer connected (/dev/usb/lp0)" || say "printer not connected"
	[ -f $CONF/no-auto-recover ] && say "automatic USB recovery: off" || say "automatic USB recovery: on"
}

case "$ACTION" in
	install) do_install ;;
	remove) do_remove ;;
	status) do_status ;;
	*) echo "usage: $0 install|remove|status"; exit 2 ;;
esac
