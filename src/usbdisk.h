#pragma once

// USB disk mode: shows the microSD card to the computer as a USB drive until BOOT is pressed (then
// restarts into the normal watch). Called from app_main in the "usb" boot mode; does not return.
void usbdisk_run(void);
