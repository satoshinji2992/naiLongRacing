#!/bin/sh

SSID=iphone17
PASSWORD=12345678
MODE=$1

echo "[racing_xiaozhi] connect WiFi: $SSID"
ifup wlan0
wapi mode wlan0 2
wapi psk wlan0 $PASSWORD 3 2
wapi essid wlan0 $SSID 1
renew wlan0

sleep 2

echo "[racing_xiaozhi] start XiaoZhi bridge"
/usr/bin/control_center &

sleep 2

amixer -c 0 cset numid=50 1
amixer -c 0 cset numid=46 1
amixer cset numid=3 60000 60000

/usr/bin/sound_app &

if test "$MODE" = "bridge"; then
  echo "[racing_xiaozhi] bridge mode, return to Racing"
  exit 0
fi

echo "[racing_xiaozhi] start Racing"
/usr/bin/racing
