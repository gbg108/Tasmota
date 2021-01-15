/*
  xdrv_84_gosund.ino - Gosund Dimmer SW2

  Copyright (C) 2020 Gabriel Gooslin

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifdef USE_LIGHT
#ifdef USE_GOSUND_DIMMER

#include <TasmotaSerial.h>

#define XDRV_84 84
#define GOSUND_BUFFER_SIZE 256

struct Gosund
{
  /* Serial data */
  TasmotaSerial *serial = nullptr;
  uint8_t *buffer = nullptr;
  uint32_t serialStream = 0;
  uint32_t syncWord = 0;

  /* LEDs */
  uint8_t powerLedPin = 12;
  uint8_t lockLedPin = 13;
  uint8_t rangeLow = 0x7F;
  uint8_t rangeHigh = 0xEB;

  /* Switch state */
  int32_t currentPower = -1;
  int32_t desiredPower = 0;
  int32_t currentBrightnessPercent = -1;
  int32_t desiredBrightnessPercent = 0;
  uint32_t state_lockout = 0;
  bool lockout = false;
} Gosund;


void GosundSetLockout(bool showLed, uint32_t delayMs) {
  if (showLed) {
    digitalWrite(Gosund.powerLedPin, HIGH);
    digitalWrite(Gosund.lockLedPin, LOW);
  }
  SetNextTimeInterval(Gosund.state_lockout, delayMs);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Set lockout %ums"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent, delayMs);
  Gosund.lockout=true;
}

bool GosundCheckLockout() {
  if (Gosund.lockout) {
    if (TimeReached(Gosund.state_lockout)) {
      digitalWrite(Gosund.powerLedPin, Gosund.currentPower ? LOW : HIGH);
      digitalWrite(Gosund.lockLedPin, HIGH);
      Gosund.lockout=false;
    }
  }

  return Gosund.lockout;
}

void GosundSerialInput(void) {
  while (Gosund.serial->available()) {
    yield();
    uint8_t serial_in_byte = Gosund.serial->read();
    if (Gosund.syncWord && Gosund.serialStream == Gosund.syncWord) {
      Gosund.desiredBrightnessPercent = serial_in_byte;
      Gosund.desiredPower=(Gosund.desiredBrightnessPercent != 0); /* If we've dimmed to 0, turn off the lights */
      LightSetDimmer(serial_in_byte);
      Gosund.serialStream=0;
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Sync word match. Read new brightness from switch"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent, serial_in_byte);
    }
    else if (((Gosund.serialStream >> 24) == 0x01) && (Gosund.syncWord != Gosund.serialStream)) {
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Switching to syncword 0x%08x"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent, Gosund.syncWord);
      Gosund.syncWord = Gosund.serialStream;
    }
    else {
      Gosund.serialStream = ((Gosund.serialStream << 8) & 0xFFFFFF00) | serial_in_byte;
    }
  }
}

void GosundSynchronize(void) {
  /* If still locked out, return */
  if (GosundCheckLockout()) return;

  /* If either our power state or brightness state are not the same, synchronize */
  if ((Gosund.currentPower != Gosund.desiredPower) ||
      (Gosund.desiredPower && (Gosund.currentBrightnessPercent != Gosund.desiredBrightnessPercent))) {
    /* Convert our bightness value from a scale 0-100 to a scale rangeLow - rangeHigh */
    uint8_t brightValue = Gosund.desiredPower ? Gosund.desiredBrightnessPercent : 0;
    brightValue = changeUIntScale(brightValue, 0, 100, Gosund.rangeLow, Gosund.rangeHigh);

    /* Inform the switch of the new brightness value */
    Gosund.serial->write(brightValue);

    /* If we're changing the power state, lock out future changes to allow the chip to get ready */
    if (Gosund.currentPower != Gosund.desiredPower)
      GosundSetLockout(true, 1000);

    Gosund.currentBrightnessPercent = Gosund.desiredBrightnessPercent;
    Gosund.currentPower = Gosund.desiredPower;

    digitalWrite(Gosund.powerLedPin, Gosund.currentPower ? LOW : HIGH);
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Changed brightness with value: 0x%02x"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent, brightValue);
  }
}

void GosundInit(void)
{
  Gosund.buffer = (uint8_t *)malloc(GOSUND_BUFFER_SIZE);
  if (Gosund.buffer != nullptr){
    Gosund.serial = new TasmotaSerial(Pin(GPIO_RXD), Pin(GPIO_TXD), 2);
    if (Gosund.serial->begin(115200)) {
      if (Gosund.serial->hardwareSerial()) {
        ClaimSerial();
      }
      Gosund.serial->flush();
   }
  }

  /* Enable LEDs and turn them off */
  pinMode(Gosund.lockLedPin, OUTPUT);
  pinMode(Gosund.powerLedPin, OUTPUT);
  digitalWrite(Gosund.lockLedPin, HIGH);
  digitalWrite(Gosund.powerLedPin, HIGH);

  /* Lock out communication with the switch for 2 seocnds to let it come online  */
  GosundSetLockout(true, 2000);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Initialized"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent);
}

bool GosundSetPower(void) {
  Gosund.desiredPower = XdrvMailbox.index;
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] Setpower"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent);
  return false;
}

bool GosundSetBrightness(void) {
  Gosund.desiredBrightnessPercent=LightGetDimmer(0);

  /* If we've dimmed to 0, turn off the lights */
  Gosund.desiredPower=(Gosund.desiredBrightnessPercent != 0);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] SetBrightness"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent);
  return false;
}

bool GosundButtonPressed(void) {
  if (!XdrvMailbox.index && ((PRESSED == XdrvMailbox.payload) && (NOT_PRESSED == Button.last_state[XdrvMailbox.index]))) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GS: [CP:%d DP:%u CB:%d DB:%u] ButtonPressed"),  Gosund.currentPower, Gosund.desiredPower, Gosund.currentBrightnessPercent, Gosund.desiredBrightnessPercent);
    ExecuteCommandPower(1, POWER_TOGGLE, SRC_LIGHT);
    return true;
  }
  return false;
}

/*
 * Interface
 */

bool Xdrv84(uint8_t function) {
  bool result = false;

  if (GOSUND_DIMMER == TasmotaGlobal.module_type) {
    switch (function) {
    case FUNC_LOOP:
      if (Gosund.serial) {
        GosundSerialInput();
        GosundSynchronize();
      }
      break;
    case FUNC_INIT:
      GosundInit();
      break;
    case FUNC_SET_DEVICE_POWER:
      result = GosundSetPower();
      break;
    case FUNC_SET_CHANNELS:
      result = GosundSetBrightness();
      break;
    case FUNC_BUTTON_PRESSED:
      result = GosundButtonPressed();
      break;
    }
  }
  return result;
}

#endif  // USE_GOSUND_DIMMER
#endif  // USE_LIGHT