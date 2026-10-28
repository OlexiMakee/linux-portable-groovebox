#!/bin/bash
set -euo pipefail

PROJECT_DIR="/Users/oleksii/Documents/XU10 Groovebox"
SSH_KEY="${PROJECT_DIR}/backups/xu10_id_ed25519"
TARGET_HOST="root@10.1.1.2"
REMOTE_DIR="/storage/dev/groovebox"

echo "=== 1. Крос-компіляція під ROCKNIX aarch64 (Zig C++) ==="
cmake -S "${PROJECT_DIR}" -B "${PROJECT_DIR}/build-xu10" \
  -DCMAKE_TOOLCHAIN_FILE="${PROJECT_DIR}/cmake/aarch64-rocknix-zig.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${PROJECT_DIR}/build-xu10" -j4

echo "=== 2. Деплой бінарника, профілів та Appliance-сервісу на MagicX XU10 (${TARGET_HOST}:${REMOTE_DIR}) ==="
ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "mkdir -p ${REMOTE_DIR}/profiles /storage/.config/system.d /storage/.config/modprobe.d && killall -9 xu10_groovebox 2>/dev/null || true"

cat "${PROJECT_DIR}/build-xu10/xu10_groovebox" | ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "cat > ${REMOTE_DIR}/xu10_groovebox && chmod +x ${REMOTE_DIR}/xu10_groovebox"

tar -C "${PROJECT_DIR}" -cf - profiles res | ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "tar -C ${REMOTE_DIR} -xf - && mkdir -p /storage/roms/groovebox && cp -rn ${REMOTE_DIR}/res/* /storage/roms/groovebox/ 2>/dev/null || true"

ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" 'bash -s' << 'REMOTE_SCRIPT'
REMOTE_DIR="/storage/dev/groovebox"

# Створюємо автономний скрипт захисту та підтримки USB Gadget (CDC-NCM / SSH 10.1.1.2)
cat << 'EOF' > ${REMOTE_DIR}/usb_watchdog.sh
#!/bin/bash
mkdir -p /storage/.cache/usbgadget /var/cache/services /storage/.cache/services
touch /var/cache/services/sshd.conf /storage/.cache/services/sshd.conf
systemctl start sshd.service 2>/dev/null || true

echo 'USB_MODE=cdc' > /storage/.cache/usbgadget/usbgadget.conf
cat << 'NET_EOF' > /storage/.cache/usbgadget/network.conf
IP=10.1.1.2
NETMASK=255.255.255.0
HOSTIP=10.1.1.1
NET_EOF

echo device > /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || true
UDC_BOUND=$(cat /sys/kernel/config/usb_gadget/cdc/UDC 2>/dev/null || echo "")
if [ -z "$UDC_BOUND" ]; then
  /usr/bin/usbgadget start cdc >/dev/null 2>&1 || true
fi
ip addr add 10.1.1.2/24 dev gadget 2>/dev/null || true
ip link set gadget up 2>/dev/null || true

HOST_IDLE_COUNT=0

while true; do
  sleep 4
  ROLE=$(cat /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || echo "")
  UDC_BOUND=$(cat /sys/kernel/config/usb_gadget/cdc/UDC 2>/dev/null || echo "")
  IP=$(ip -4 addr show gadget 2>/dev/null | grep '10\.1\.1\.2' || echo "")
  HOST_PORT_STATE=$(cat /sys/bus/usb/devices/usb1/1-0:1.0/usb1-port1/state 2>/dev/null || echo "not attached")

  if [ "$ROLE" = "host" ]; then
    if [ "$HOST_PORT_STATE" = "not attached" ]; then
      HOST_IDLE_COUNT=$((HOST_IDLE_COUNT + 1))
      if [ "$HOST_IDLE_COUNT" -ge 3 ]; then
        # 3 перевірки (~12с) без підключеного USB MIDI пристрою: безпечно повертаємося в device mode
        echo device > /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || true
        /usr/bin/usbgadget restart cdc >/dev/null 2>&1 || true
        ip addr add 10.1.1.2/24 dev gadget 2>/dev/null || true
        ip link set gadget up 2>/dev/null || true
        HOST_IDLE_COUNT=0
      fi
    else
      HOST_IDLE_COUNT=0
    fi
  else
    HOST_IDLE_COUNT=0
    if [ "$ROLE" != "device" ]; then
      echo device > /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || true
    fi
    if [ -z "$UDC_BOUND" ]; then
      /usr/bin/usbgadget start cdc >/dev/null 2>&1 || true
    fi
    if [ -z "$IP" ]; then
      ip addr add 10.1.1.2/24 dev gadget 2>/dev/null || true
      ip link set gadget up 2>/dev/null || true
    fi
  fi
done
EOF
chmod +x ${REMOTE_DIR}/usb_watchdog.sh

# Створюємо системний сервіс постійного утримання USB Gadget мережі
cat << 'EOF' > /storage/.config/system.d/usb-gadget-device.service
[Unit]
Description=USB Gadget CDC-NCM Device & SSH Keepalive Watchdog
After=network.target

[Service]
Type=simple
ExecStart=/storage/dev/groovebox/usb_watchdog.sh
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target rocknix.target
EOF

cat << 'EOF' > ${REMOTE_DIR}/launch_groovebox.sh
#!/bin/bash
mkdir -p /var/cache/services /storage/.cache/services /storage/.config/modprobe.d /storage/.cache/usbgadget
touch /var/cache/services/sshd.conf /storage/.cache/services/sshd.conf
echo 'options snd-usb-audio autoclock=0 lowlatency=0' > /storage/.config/modprobe.d/snd-usb-audio.conf 2>/dev/null || true
systemctl start sshd.service 2>/dev/null || true

# Гарантуємо, що USB OTG порт завжди перебуває в режимі Device (CDC-NCM / SSH 10.1.1.2)
echo device > /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || true
echo 'USB_MODE=cdc' > /storage/.cache/usbgadget/usbgadget.conf
cat << 'NET_EOF' > /storage/.cache/usbgadget/network.conf
IP=10.1.1.2
NETMASK=255.255.255.0
HOSTIP=10.1.1.1
NET_EOF
UDC_BOUND=$(cat /sys/kernel/config/usb_gadget/cdc/UDC 2>/dev/null || echo "")
if [ -z "$UDC_BOUND" ]; then
  /usr/bin/usbgadget start cdc >/dev/null 2>&1 || true
fi

modprobe snd-usb-audio autoclock=0 lowlatency=0 2>/dev/null || true
modprobe snd-rawmidi 2>/dev/null || true
modprobe snd-seq-midi 2>/dev/null || true

# Фіксуємо переривання USB dwc2 (IRQ 26) на виділене ядро CPU 2 (усуває конкуренцію з дисплеєм на CPU 0)
echo 4 > /proc/irq/26/smp_affinity 2>/dev/null || true

# Фіксуємо CPU Governor у performance (1.5 GHz), щоб уникнути тріску/лагів на USB dwc2
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true

# Зупиняємо всі фонові сервіси, які засмічують звук і шину
systemctl stop pipewire-pulse.socket pipewire.socket pipewire-pulse pipewire avahi-daemon simple-http-server 2>/dev/null || true

# Прямий вивід на екран через Direct KMSDRM (нуль оверхеду Wayland/Mali)
export SDL_VIDEODRIVER=kmsdrm
export SDL_RENDER_DRIVER=software

exec /storage/dev/groovebox/xu10_groovebox
EOF
chmod +x ${REMOTE_DIR}/launch_groovebox.sh

# 1. Створюємо системний сервіс виділеного Appliance у systemd
cat << 'EOF' > /storage/.config/system.d/groovebox.service
[Unit]
Description=MagicX XU10 Dedicated Groovebox DAW Appliance
After=sound.target rocknix.target
Wants=sound.target
Conflicts=essway.service sway.service

[Service]
Type=simple
WorkingDirectory=/storage/dev/groovebox
ExecStart=/storage/dev/groovebox/launch_groovebox.sh
Restart=always
RestartSec=1
StandardOutput=journal+console
StandardError=journal+console

[Install]
WantedBy=rocknix.target
EOF

# 2. Назавжди маскуємо EmulationStation та Sway (Wayland), активуємо groovebox.service та usb-gadget-device.service
systemctl mask essway.service sway.service 2>/dev/null || true
systemctl enable /storage/.config/system.d/groovebox.service 2>/dev/null || true
systemctl enable /storage/.config/system.d/usb-gadget-device.service 2>/dev/null || true
systemctl restart usb-gadget-device.service 2>/dev/null || true
rm -f /storage/.config/autostart/99-groovebox.sh 2>/dev/null || true

sync
REMOTE_SCRIPT

if [[ "${1:-}" == "--run" ]]; then
  echo "=== 3. Запуск xu10_groovebox як Dedicated Appliance (Direct KMSDRM + Dedicated Audio CPU3) ==="
  ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" "
    echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true
    modprobe snd-usb-audio autoclock=0 lowlatency=0 2>/dev/null || true
    modprobe snd-rawmidi 2>/dev/null || true
    modprobe snd-seq-midi 2>/dev/null || true
    echo 4 > /proc/irq/26/smp_affinity 2>/dev/null || true
    systemctl stop pipewire-pulse.socket pipewire.socket pipewire-pulse pipewire avahi-daemon simple-http-server 2>/dev/null || true
    export SDL_VIDEODRIVER=kmsdrm
    killall -9 xu10_groovebox 2>/dev/null || true
    nohup /storage/dev/groovebox/launch_groovebox.sh >/tmp/groovebox.log 2>&1 &
    sleep 1
    ps aux | grep xu10_groovebox | grep -v grep
    cat /tmp/groovebox.log
  "
fi
