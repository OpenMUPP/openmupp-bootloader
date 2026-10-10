#include "UsbCdc.h"

#include <cmsis_device.h>
#include <cstring>

namespace usbcdc {

namespace {

// ---------------------------------------------------------------- descriptors

constexpr uint16_t vendorId = 0x0483;
constexpr uint16_t productId = 0x5740;

const uint8_t deviceDescriptor[18] = {
   18, 1, 0x00, 0x02,      // bLength, DEVICE, bcdUSB 2.00
   0x02, 0x00, 0x00, 64,   // class CDC, subclass, protocol, bMaxPacketSize0
   vendorId & 0xFF, vendorId >> 8, productId & 0xFF, productId >> 8,
   0x00, 0x02,             // bcdDevice 2.00
   1, 2, 3, 1,             // iManufacturer, iProduct, iSerialNumber, bNumConfigurations
};

const uint8_t configurationDescriptor[67] = {
   9, 2, 67, 0, 2, 1, 0, 0x80, 50,           // CONFIGURATION, 2 interfaces, bus powered, 100 mA
   9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,       // INTERFACE 0: CDC ACM, 1 endpoint
   5, 0x24, 0x00, 0x10, 0x01,                // CDC header 1.10
   5, 0x24, 0x01, 0x00, 1,                   // call management, data interface 1
   4, 0x24, 0x02, 0x02,                      // ACM: line coding and control line state
   5, 0x24, 0x06, 0, 1,                      // union: control 0, data 1
   7, 5, 0x81, 0x03, 8, 0, 255,              // EP1 IN interrupt, 8 bytes
   9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,       // INTERFACE 1: CDC data, 2 endpoints
   7, 5, 0x02, 0x02, 64, 0, 0,               // EP2 OUT bulk, 64 bytes
   7, 5, 0x82, 0x02, 64, 0, 0,               // EP2 IN bulk, 64 bytes
};

const uint8_t languageString[4] = { 4, 3, 0x09, 0x04 };
const uint8_t manufacturerString[] = { 20, 3, 'V', 0, 'e', 0, 'r', 0, 's', 0, 'a', 0, 'l', 0, 'o', 0, 'o', 0, 'n', 0 };
const uint8_t productString[] = { 22, 3, 'B', 0, 'o', 0, 'o', 0, 't', 0, 'l', 0, 'o', 0, 'a', 0, 'd', 0, 'e', 0, 'r', 0 };
uint8_t serialString[2 + 2 * 24];

// ------------------------------------------------------------- packet memory

// The packet memory (PMA) has 512 bytes. The CPU sees each 16-bit word at a
// 32-bit address step.
constexpr uint16_t ep0Tx = 0x40;
constexpr uint16_t ep0Rx = 0x80;
constexpr uint16_t ep1Tx = 0xC0;
constexpr uint16_t ep2Rx = 0x100;
constexpr uint16_t ep2Tx = 0x140;
// COUNT_RX: 32-byte blocks (BL_SIZE), 2 blocks = 64 bytes
constexpr uint16_t rxCount64 = 0x8000 | (1 << 10);

volatile uint32_t *pma(uint16_t offset) {
   return reinterpret_cast<volatile uint32_t *>(USB_PMAADDR + 2u * offset);
}

void pmaWrite(uint16_t offset, const uint8_t *data, std::size_t size) {
   volatile uint32_t *word = pma(offset);
   for(std::size_t i = 0; i < size; i += 2) {
      uint16_t value = data[i];
      if(i + 1 < size) {
         value |= static_cast<uint16_t>(data[i + 1] << 8);
      }
      *word++ = value;
   }
}

void pmaRead(uint16_t offset, uint8_t *data, std::size_t size) {
   volatile const uint32_t *word = pma(offset);
   for(std::size_t i = 0; i < size; i += 2) {
      const uint32_t value = *word++;
      data[i] = static_cast<uint8_t>(value);
      if(i + 1 < size) {
         data[i + 1] = static_cast<uint8_t>(value >> 8);
      }
   }
}

// Buffer descriptor table at PMA offset 0: ADDR_TX, COUNT_TX, ADDR_RX,
// COUNT_RX for each endpoint.
volatile uint32_t &bdt(uint8_t ep, uint8_t field) {
   return *pma(static_cast<uint16_t>(ep * 8 + field * 2));
}
constexpr uint8_t addrTx = 0, countTx = 1, addrRx = 2, countRx = 3;

// ------------------------------------------------------------ endpoint register

// Bits that a write keeps: the CTR flags are "write 0 to clear", the STAT
// and DTOG bits are "write 1 to toggle".
constexpr uint16_t epKeep = USB_EP_CTR_RX | USB_EP_SETUP | USB_EP_T_FIELD | USB_EP_KIND | USB_EP_CTR_TX |
                            USB_EPADDR_FIELD;
constexpr uint16_t statTx = 0x0030;
constexpr uint16_t statRx = 0x3000;
constexpr uint16_t dtogTx = 0x0040;
constexpr uint16_t dtogRx = 0x4000;

volatile uint16_t &epr(uint8_t ep) {
   return (&USB->EP0R)[2 * ep];
}

// Sets the STAT bits in mask (statTx and/or statRx) to value.
void setStat(uint8_t ep, uint16_t mask, uint16_t value) {
   const uint16_t reg = epr(ep);
   epr(ep) = static_cast<uint16_t>(((reg & (epKeep | mask)) ^ value) | USB_EP_CTR_RX | USB_EP_CTR_TX);
}

void clearCtr(uint8_t ep, uint16_t flag) {
   epr(ep) = static_cast<uint16_t>((epr(ep) & epKeep & ~flag) | ((USB_EP_CTR_RX | USB_EP_CTR_TX) & ~flag));
}

// Opens an endpoint: type, address, DTOG bits cleared, the given states.
void openEndpoint(uint8_t ep, uint16_t type, uint16_t stat) {
   const uint16_t reg = epr(ep);
   // Toggle the DTOG and STAT bits from their current value to the wanted value.
   epr(ep) = static_cast<uint16_t>(((reg & (dtogTx | dtogRx | statTx | statRx)) ^ stat) | type | ep | USB_EP_CTR_RX |
                                   USB_EP_CTR_TX);
}

// ----------------------------------------------------------------- state

struct Setup {
   uint8_t bmRequestType;
   uint8_t bRequest;
   uint16_t wValue;
   uint16_t wIndex;
   uint16_t wLength;
};

const uint8_t *ctrlData;    // data stage IN: remaining data
uint16_t ctrlLeft;          // data stage IN: remaining bytes
bool ctrlZlp;               // data stage IN: end with a zero-length packet
bool ctrlReceive;           // data stage OUT: line coding expected
uint8_t pendingAddress;     // SET_ADDRESS: applied after the status stage
uint8_t configuration;
uint32_t resets;
uint8_t lineCoding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 }; // 115200 8N1
uint8_t replyBuffer[2];

uint8_t rxPacket[packetSize];
uint8_t rxLength;
uint8_t rxOffset;
bool rxFull;

uint8_t txQueue[txCapacity];
uint16_t txLength;
bool txBusy;
bool txZlp;

void (*pullUpCallback)(bool);

// -------------------------------------------------------------- EP0 control

void ep0SendNext() {
   const uint16_t n = ctrlLeft < packetSize ? ctrlLeft : packetSize;
   pmaWrite(ep0Tx, ctrlData, n);
   bdt(0, countTx) = n;
   ctrlData += n;
   ctrlLeft = static_cast<uint16_t>(ctrlLeft - n);
   if(n < packetSize) {
      ctrlZlp = false; // a short packet ends the transfer
   }
   setStat(0, statTx, USB_EP_TX_VALID);
}

void ep0Reply(const uint8_t *data, uint16_t size, uint16_t wLength) {
   ctrlData = data;
   ctrlLeft = size < wLength ? size : wLength;
   // A reply shorter than wLength ends with a short packet; a multiple of
   // the packet size needs a zero-length packet.
   ctrlZlp = ctrlLeft < wLength && ctrlLeft % packetSize == 0;
   ep0SendNext();
}

void ep0Status() {
   bdt(0, countTx) = 0;
   ctrlLeft = 0;
   ctrlZlp = false;
   setStat(0, statTx, USB_EP_TX_VALID);
}

void ep0Stall() {
   setStat(0, statTx | statRx, USB_EP_TX_STALL | USB_EP_RX_VALID);
}

void closeDataEndpoints() {
   for(uint8_t ep = 1; ep <= 2; ++ep) {
      openEndpoint(ep, ep == 1 ? USB_EP_INTERRUPT : USB_EP_BULK, 0); // DISABLED
   }
   txBusy = false;
   txLength = 0;
   txZlp = false;
   rxFull = false;
}

void openDataEndpoints() {
   openEndpoint(1, USB_EP_INTERRUPT, USB_EP_TX_NAK);
   openEndpoint(2, USB_EP_BULK, USB_EP_TX_NAK | USB_EP_RX_VALID);
}

const uint8_t *stringDescriptor(uint8_t index) {
   switch(index) {
   case 0: return languageString;
   case 1: return manufacturerString;
   case 2: return productString;
   case 3: return serialString;
   default: return nullptr;
   }
}

// Returns false for a request that gets a STALL.
bool standardRequest(const Setup &s) {
   const uint8_t recipient = s.bmRequestType & 0x1F;
   switch(s.bRequest) {
   case 0x06: { // GET_DESCRIPTOR
      const auto type = static_cast<uint8_t>(s.wValue >> 8);
      if(type == 1) {
         ep0Reply(deviceDescriptor, sizeof deviceDescriptor, s.wLength);
      } else if(type == 2) {
         ep0Reply(configurationDescriptor, sizeof configurationDescriptor, s.wLength);
      } else if(const uint8_t *d = type == 3 ? stringDescriptor(static_cast<uint8_t>(s.wValue)) : nullptr) {
         ep0Reply(d, d[0], s.wLength);
      } else {
         return false;
      }
      return true;
   }
   case 0x05: // SET_ADDRESS
      pendingAddress = s.wValue & 0x7F;
      ep0Status();
      return true;
   case 0x09: // SET_CONFIGURATION
      if(s.wValue > 1) {
         return false;
      }
      configuration = static_cast<uint8_t>(s.wValue);
      if(configuration) {
         openDataEndpoints();
      } else {
         closeDataEndpoints();
      }
      ep0Status();
      return true;
   case 0x08: // GET_CONFIGURATION
      replyBuffer[0] = configuration;
      ep0Reply(replyBuffer, 1, s.wLength);
      return true;
   case 0x00: // GET_STATUS
      replyBuffer[0] = 0;
      replyBuffer[1] = 0;
      if(recipient == 2) {
         const uint8_t ep = s.wIndex & 0x0F;
         const uint16_t stat = ep == 0 ? 0 : (s.wIndex & 0x80 ? epr(ep) & statTx : epr(ep) & statRx);
         replyBuffer[0] = stat == USB_EP_TX_STALL || stat == USB_EP_RX_STALL;
      }
      ep0Reply(replyBuffer, 2, s.wLength);
      return true;
   case 0x01: // CLEAR_FEATURE
   case 0x03: // SET_FEATURE
      if(recipient == 2 && s.wValue == 0 && (s.wIndex & 0x0F) != 0 && configuration) {
         // ENDPOINT_HALT on a data endpoint
         const uint8_t ep = s.wIndex & 0x0F;
         const bool in = s.wIndex & 0x80;
         const uint16_t mask = in ? statTx : statRx;
         if(s.bRequest == 0x03) {
            setStat(ep, mask, in ? USB_EP_TX_STALL : USB_EP_RX_STALL);
         } else {
            // Clear the halt: reset the data toggle and resume.
            const uint16_t reg = epr(ep);
            const uint16_t dtog = in ? dtogTx : dtogRx;
            const uint16_t resume = in ? (txBusy ? USB_EP_TX_VALID : USB_EP_TX_NAK)
                                       : (rxFull ? USB_EP_RX_NAK : USB_EP_RX_VALID);
            epr(ep) = static_cast<uint16_t>(((reg & (epKeep | dtog | mask)) ^ resume) | USB_EP_CTR_RX | USB_EP_CTR_TX);
         }
         ep0Status();
         return true;
      }
      if(recipient == 0) {
         ep0Status(); // DEVICE_REMOTE_WAKEUP: not supported, ignored
         return true;
      }
      return false;
   case 0x0A: // GET_INTERFACE
      replyBuffer[0] = 0;
      ep0Reply(replyBuffer, 1, s.wLength);
      return true;
   case 0x0B: // SET_INTERFACE
      if(s.wValue != 0) {
         return false;
      }
      ep0Status();
      return true;
   default:
      return false;
   }
}

bool classRequest(const Setup &s) {
   switch(s.bRequest) {
   case 0x20: // SET_LINE_CODING
      if(s.wLength != sizeof lineCoding) {
         return false;
      }
      ctrlReceive = true;
      return true;
   case 0x21: // GET_LINE_CODING
      ep0Reply(lineCoding, sizeof lineCoding, s.wLength);
      return true;
   case 0x22: // SET_CONTROL_LINE_STATE
   case 0x23: // SEND_BREAK
      ep0Status();
      return true;
   default:
      return false;
   }
}

void ep0Setup() {
   uint8_t raw[8];
   pmaRead(ep0Rx, raw, sizeof raw);
   const Setup s{ raw[0], raw[1], static_cast<uint16_t>(raw[2] | raw[3] << 8),
                  static_cast<uint16_t>(raw[4] | raw[5] << 8), static_cast<uint16_t>(raw[6] | raw[7] << 8) };
   ctrlLeft = 0;
   ctrlZlp = false;
   ctrlReceive = false;
   const uint8_t type = s.bmRequestType & 0x60;
   const bool ok = type == 0x00 ? standardRequest(s) : type == 0x20 ? classRequest(s) : false;
   if(!ok) {
      ep0Stall();
      return;
   }
   setStat(0, statRx, USB_EP_RX_VALID);
}

void ep0Out() {
   if(ctrlReceive) {
      ctrlReceive = false;
      if((bdt(0, countRx) & 0x3FF) == sizeof lineCoding) {
         pmaRead(ep0Rx, lineCoding, sizeof lineCoding);
      }
      ep0Status();
   }
   setStat(0, statRx, USB_EP_RX_VALID);
}

void ep0In() {
   if(pendingAddress) {
      USB->DADDR = static_cast<uint16_t>(USB_DADDR_EF | pendingAddress);
      pendingAddress = 0;
   }
   if(ctrlLeft || ctrlZlp) {
      ep0SendNext();
   }
}

// -------------------------------------------------------------- EP2 data

void sendNextPacket() {
   if(txBusy || !configuration) {
      return;
   }
   if(txLength == 0 && !txZlp) {
      return;
   }
   const uint16_t n = txLength < packetSize ? txLength : packetSize;
   pmaWrite(ep2Tx, txQueue, n);
   bdt(2, countTx) = n;
   txLength = static_cast<uint16_t>(txLength - n);
   std::memmove(txQueue, txQueue + n, txLength);
   // A full last packet needs a zero-length packet after it.
   txZlp = n == packetSize && txLength == 0;
   txBusy = true;
   setStat(2, statTx, USB_EP_TX_VALID);
}

void busReset() {
   USB->BTABLE = 0;
   bdt(0, addrTx) = ep0Tx;
   bdt(0, addrRx) = ep0Rx;
   bdt(0, countRx) = rxCount64;
   bdt(1, addrTx) = ep1Tx;
   bdt(1, countTx) = 0;
   bdt(2, addrTx) = ep2Tx;
   bdt(2, addrRx) = ep2Rx;
   bdt(2, countRx) = rxCount64;
   openEndpoint(0, USB_EP_CONTROL, USB_EP_TX_NAK | USB_EP_RX_VALID);
   closeDataEndpoints();
   configuration = 0;
   pendingAddress = 0;
   ctrlLeft = 0;
   ctrlReceive = false;
   USB->DADDR = USB_DADDR_EF;
   ++resets;
}

} // namespace

void start(const uint8_t (&uid)[12], void (*pullUp)(bool on)) {
   static const char hex[] = "0123456789ABCDEF";
   serialString[0] = sizeof serialString;
   serialString[1] = 3;
   for(int i = 0; i < 24; ++i) {
      serialString[2 + 2 * i] = hex[(uid[i / 2] >> (i & 1 ? 0 : 4)) & 0xF];
   }

   pullUpCallback = pullUp;
   // Power-up (RM0008 23.4.2): clear PDWN, wait tSTARTUP (1 us), release FRES.
   USB->CNTR = USB_CNTR_FRES;
   for(int i = 0; i < 100; ++i) {
      __NOP();
   }
   USB->CNTR = 0;
   USB->ISTR = 0;
   busReset();
   if(pullUpCallback) {
      pullUpCallback(true);
   }
}

void poll() {
   const uint16_t istr = USB->ISTR;
   if(istr & USB_ISTR_RESET) {
      USB->ISTR = static_cast<uint16_t>(~USB_ISTR_RESET);
      busReset();
      return;
   }
   if(istr & (USB_ISTR_SUSP | USB_ISTR_WKUP)) {
      USB->ISTR = static_cast<uint16_t>(~(USB_ISTR_SUSP | USB_ISTR_WKUP));
   }
   while(USB->ISTR & USB_ISTR_CTR) {
      const uint8_t ep = USB->ISTR & USB_ISTR_EP_ID;
      const uint16_t reg = epr(ep);
      if(ep == 0) {
         if(reg & USB_EP_CTR_TX) {
            clearCtr(0, USB_EP_CTR_TX);
            ep0In();
         }
         if(reg & USB_EP_CTR_RX) {
            clearCtr(0, USB_EP_CTR_RX);
            if(reg & USB_EP_SETUP) {
               ep0Setup();
            } else {
               ep0Out();
            }
         }
      } else {
         if(reg & USB_EP_CTR_TX) {
            clearCtr(ep, USB_EP_CTR_TX);
            txBusy = false;
         }
         if(reg & USB_EP_CTR_RX) {
            clearCtr(ep, USB_EP_CTR_RX);
            // The hardware sets the endpoint to NAK; read() re-arms it.
            rxLength = static_cast<uint8_t>(bdt(2, countRx) & 0x3FF);
            pmaRead(ep2Rx, rxPacket, rxLength);
            rxOffset = 0;
            rxFull = true;
            if(rxLength == 0) {
               rxFull = false;
               setStat(2, statRx, USB_EP_RX_VALID);
            }
         }
      }
   }
   sendNextPacket();
}

std::size_t read(uint8_t *data, std::size_t size) {
   if(!rxFull) {
      return 0;
   }
   std::size_t n = static_cast<std::size_t>(rxLength - rxOffset);
   if(n > size) {
      n = size;
   }
   std::memcpy(data, rxPacket + rxOffset, n);
   rxOffset = static_cast<uint8_t>(rxOffset + n);
   if(rxOffset == rxLength) {
      rxFull = false;
      setStat(2, statRx, USB_EP_RX_VALID);
   }
   return n;
}

std::size_t write(const uint8_t *data, std::size_t size) {
   std::size_t n = txCapacity - txLength;
   if(n > size) {
      n = size;
   }
   std::memcpy(txQueue + txLength, data, n);
   txLength = static_cast<uint16_t>(txLength + n);
   return n;
}

bool configured() {
   return configuration != 0;
}

uint32_t resetCount() {
   return resets;
}

} // namespace usbcdc
