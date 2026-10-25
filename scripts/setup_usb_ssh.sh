#!/bin/bash
set -euo pipefail

PROJECT_DIR="/Users/oleksii/Documents/XU10 Groovebox"
PUBKEY=$(cat "${PROJECT_DIR}/backups/xu10_id_ed25519.pub")

echo "=== 1. Встановлення правильного MAC консолі (82:ba:1c:92:78:b1) на en9 ==="
sudo ifconfig en9 inet 169.254.170.1 netmask 255.255.255.0 up
sudo route -n change -host 169.254.170.2 -link 82:ba:1c:92:78:b1 2>/dev/null || sudo arp -S 169.254.170.2 82:ba:1c:92:78:b1
sudo arp -s 169.254.170.2 82:ba:1c:92:78:b1 ifscope en9 2>/dev/null || true

echo "=== 2. Перевірка ping до MagicX XU10 (169.254.170.2) ==="
ping -c 2 169.254.170.2

echo "=== 3. Встановлення SSH-ключа та перемикання USB Gadget на 10.1.1.2/24 ==="
echo "УВАГА: Зараз введи новий root-пароль, який ти встановив на консолі!"
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@169.254.170.2 "
  mkdir -p /storage/.ssh /storage/dev/groovebox /storage/.cache/services /storage/.cache/usbgadget &&
  touch /storage/.cache/services/sshd.conf &&
  grep -qF '${PUBKEY}' /storage/.ssh/authorized_keys 2>/dev/null || echo '${PUBKEY}' >> /storage/.ssh/authorized_keys &&
  chmod 700 /storage/.ssh &&
  chmod 600 /storage/.ssh/authorized_keys &&
  cat << 'EOF' > /storage/.cache/usbgadget/network.conf
IP=10.1.1.2
NETMASK=255.255.255.0
HOSTIP=10.1.1.1
EOF
  echo '=== XU10 HARDWARE INVENTORY ===' &&
  uname -a &&
  cat /proc/asound/cards &&
  grep -E 'Name=|Handlers=' /proc/bus/input/devices &&
  head -n 5 /sys/class/usb_role/*/role /sys/class/udc/*/state &&
  nohup sh -c 'sleep 1 && /usr/bin/usbgadget restart cdc' >/dev/null 2>&1 &
"

echo "=== 4. Отримання нової адреси 10.1.1.1 по DHCP на en9 ==="
sleep 3
sudo ipconfig set en9 DHCP
sleep 2
ifconfig en9
ping -c 2 10.1.1.2
ssh -i "${PROJECT_DIR}/backups/xu10_id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@10.1.1.2 "echo 'SSH PASSWORDLESS TO 10.1.1.2: OK!'"
