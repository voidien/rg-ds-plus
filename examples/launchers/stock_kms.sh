#!/bin/sh
# Run one of the direct-KMS examples on the stock firmware: Weston must release the displays first.
#   stock_kms.sh kms_dualscreen         (CPU-drawn buffers)
#   stock_kms.sh gbm_egl_dualscreen     (GPU, GBM + EGL + GLES2)
# Put this script and the binaries in Roms/APPS (one launcher .sh per example is easiest: copy this
# file and give it the fixed name inside). The screens go dark when Weston stops, so the script
# reboots at the end to bring the normal UI back; that is deliberate, since restarting Weston and
# the launcher by hand is unverified.
# UNTESTED on hardware.
cd "$(dirname "$0")" || exit 1
PROG=${1:-kms_dualscreen}
LOG=./$PROG.log
chmod +x "./$PROG" 2>/dev/null
/etc/init.d/S49weston stop > "$LOG" 2>&1
sleep 1
"./$PROG" 8 >> "$LOG" 2>&1
sync
reboot
