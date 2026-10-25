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

echo "=== 2. Деплой бінарника, профілів та автостарту на MagicX XU10 (${TARGET_HOST}:${REMOTE_DIR}) ==="
ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "mkdir -p ${REMOTE_DIR}/profiles /storage/roms/ports /storage/.config/autostart && killall -9 xu10_groovebox 2>/dev/null || true"

cat "${PROJECT_DIR}/build-xu10/xu10_groovebox" | ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "cat > ${REMOTE_DIR}/xu10_groovebox && chmod +x ${REMOTE_DIR}/xu10_groovebox"

tar -C "${PROJECT_DIR}" -cf - profiles | ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" \
  "tar -C ${REMOTE_DIR} -xf -"

ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" "
cat << 'EOF' > ${REMOTE_DIR}/launch_groovebox.sh
#!/bin/bash
mkdir -p /var/cache/services /storage/.cache/services /storage/.config/modprobe.d
touch /var/cache/services/sshd.conf /storage/.cache/services/sshd.conf
echo "options snd-usb-audio implicit_fb=1,1,1,1 lowlatency=0" > /storage/.config/modprobe.d/snd-usb-audio.conf 2>/dev/null || true
systemctl start sshd.service 2>/dev/null || true
modprobe snd-usb-audio implicit_fb=1,1,1,1 lowlatency=0 2>/dev/null || true
modprobe snd-rawmidi 2>/dev/null || true
modprobe snd-seq-midi 2>/dev/null || true

# Фіксуємо CPU Governor у performance (1.5 GHz), щоб уникнути тріску/лагів на USB dwc2
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true

# Якщо консоль НЕ підключена до комп'ютера по USB-C (UDC не configured),
# автоматично переводимо порт OTG у режим HOST і вмикаємо 5V VBUS (OTG_SWITCH) для Digitakt / Хаба / MIDI!
UDC_STATE=\$(cat /sys/class/udc/ff300000.usb/state 2>/dev/null || echo 'none')
if [ \"\${UDC_STATE}\" != \"configured\" ]; then
  echo '' > /sys/kernel/config/usb_gadget/cdc/UDC 2>/dev/null || true
  echo host > /sys/class/usb_role/ff300000.usb-role-switch/role 2>/dev/null || true
  /usr/sbin/i2cset -f -y 0 0x20 0xb4 0x46 2>/dev/null || true
fi

systemctl stop pipewire-pulse.socket pipewire.socket pipewire-pulse pipewire 2>/dev/null || true
kill -STOP \$(pidof emulationstation) 2>/dev/null || true
kill -STOP \$(pidof input_sense) \$(pidof evtest) \$(pidof mako) 2>/dev/null || true

export XDG_RUNTIME_DIR=/var/run/0-runtime-dir
export WAYLAND_DISPLAY=wayland-1
export SWAYSOCK=/var/run/0-runtime-dir/sway-ipc.0.sock
export SDL_VIDEODRIVER=wayland

/storage/dev/groovebox/xu10_groovebox >/tmp/groovebox.log 2>&1

kill -CONT \$(pidof input_sense) \$(pidof evtest) \$(pidof mako) 2>/dev/null || true
kill -CONT \$(pidof emulationstation) 2>/dev/null || true
systemctl start pipewire.socket pipewire-pulse.socket 2>/dev/null || true
EOF
chmod +x ${REMOTE_DIR}/launch_groovebox.sh

# 1. Ярлик у меню Ports для запуску з інтерфейсу консолі в один клік
cp ${REMOTE_DIR}/launch_groovebox.sh /storage/roms/ports/XU10_Groovebox.sh
chmod +x /storage/roms/ports/XU10_Groovebox.sh

# 2. Автозапуск DAW одразу після завантаження консолі
cat << 'EOF' > /storage/.config/autostart/99-groovebox.sh
#!/bin/bash
(
  for _ in \$(seq 1 30); do
    [ -S /var/run/0-runtime-dir/wayland-1 ] && break
    sleep 0.5
  done
  sleep 1
  /storage/dev/groovebox/launch_groovebox.sh
) &
EOF
chmod +x /storage/.config/autostart/99-groovebox.sh
sync
"

if [[ "${1:-}" == "--run" ]]; then
  echo "=== 3. Запуск xu10_groovebox на екрані консолі (Wayland/Sway + Direct ALSA) ==="
  ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "${TARGET_HOST}" "
    echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true
    modprobe snd-usb-audio implicit_fb=1,1,1,1 lowlatency=0 2>/dev/null || true
modprobe snd-rawmidi 2>/dev/null || true
modprobe snd-seq-midi 2>/dev/null || true
    systemctl stop pipewire-pulse.socket pipewire.socket pipewire-pulse pipewire 2>/dev/null || true
    kill -STOP \$(pidof emulationstation) 2>/dev/null || true
    kill -STOP \$(pidof input_sense) \$(pidof evtest) \$(pidof mako) 2>/dev/null || true
    export XDG_RUNTIME_DIR=/var/run/0-runtime-dir
    export WAYLAND_DISPLAY=wayland-1
    export SWAYSOCK=/var/run/0-runtime-dir/sway-ipc.0.sock
    export SDL_VIDEODRIVER=wayland
    nohup ${REMOTE_DIR}/xu10_groovebox >/tmp/groovebox.log 2>&1 &
    sleep 1
    ps aux | grep xu10_groovebox | grep -v grep
    cat /tmp/groovebox.log
  "
fi
