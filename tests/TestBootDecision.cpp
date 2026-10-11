#include <doctest/doctest.h>

#include "boot/BootDecision.h"

using boot::Decision;

namespace {

struct Probe {
   bool keyDown = false;
   bool applicationValid = true;
   int keyReads = 0;
   int checks = 0;

   Decision decide(uint16_t dr1, uint16_t dr2) {
      return boot::decide(
      dr1, dr2,
      [this] {
         ++keyReads;
         return keyDown;
      },
      [this] {
         ++checks;
         return applicationValid;
      });
   }
};

} // namespace

TEST_CASE("with no request, key up and a valid application, the application starts") {
   Probe probe;
   CHECK(probe.decide(0, 0) == Decision::application);
   CHECK(probe.checks == 1);
}

TEST_CASE("the boot request keeps the bootloader, also with a GO token") {
   Probe probe;
   CHECK(probe.decide(boot::bootRequest, 0) == Decision::bootloader);
   CHECK(probe.decide(boot::bootRequest, boot::goToken) == Decision::bootloader);
   // No key read and no CRC check.
   CHECK(probe.keyReads == 0);
   CHECK(probe.checks == 0);
}

TEST_CASE("the GO token starts the application with no key read and no check") {
   Probe probe;
   probe.keyDown = true;
   probe.applicationValid = false;
   CHECK(probe.decide(0, boot::goToken) == Decision::application);
   CHECK(probe.keyReads == 0);
   CHECK(probe.checks == 0);
}

TEST_CASE("the key keeps the bootloader with no CRC check") {
   Probe probe;
   probe.keyDown = true;
   CHECK(probe.decide(0, 0) == Decision::bootloader);
   CHECK(probe.checks == 0);
}

TEST_CASE("an application that is not valid keeps the bootloader") {
   Probe probe;
   probe.applicationValid = false;
   CHECK(probe.decide(0, 0) == Decision::bootloader);
}

TEST_CASE("other register values are no request") {
   Probe probe;
   CHECK(probe.decide(0xFFFF, 0xFFFF) == Decision::application);
   CHECK(probe.decide(boot::goToken, boot::bootRequest) == Decision::application);
   CHECK(probe.checks == 2);
}
