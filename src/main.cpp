// Entry point of the rewritten bootloader (issue #4).
//
// This is the empty image of #7: the start-up code of system-stm32 runs,
// then main() waits. #10 adds the boot decision, the USB CDC port
// (usb/UsbCdc.h) and the AN3155 protocol.

int main() {
   for(;;) {
   }
}
