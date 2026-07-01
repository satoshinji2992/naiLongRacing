# racing_xiaozhi.sh -- NSH script. Run this once from NSH:
# sh /data/racing_xiaozhi.sh
#
# Do not run this again from inside Racing; control_center owns UDP 5676/5678
# and Racing owns UDP 5679. Starting a second control_center will bind-fail.

echo racing_xiaozhi: connect WiFi iphone17
ifup wlan0
wapi mode wlan0 2
wapi psk wlan0 12345678 3 2
wapi essid wlan0 iphone17 1
renew wlan0
sleep 2

echo racing_xiaozhi: start audio
amixer set 19 255
amixer set 20 255
amixer set 21 255
amixer set 6 180
amixer set 7 180
amixer set 15 6
arecord -D hw:snddmic -r 48000 -f 16 -c 2 -o &
aplay -D hw:audiocodec -r 48000 -f 16 -c 2 -o &

echo racing_xiaozhi: start control_center
control_center &
sleep 2

echo racing_xiaozhi: start racing
racing
