/**
 * Author Wojciech Domski <Wojciech.Domski@gmail.com>
 * www: www.Domski.pl
 *
 * Hardware layer for SX1278 LoRa module
 */

#include "SX1278_hw_arduino.h"
#include <string.h>

#include <Arduino.h>
#include <SPI.h>

__weak void SX1278_hw_init(SX1278_hw_t *hw) {
	pinMode(hw->reset.pin, OUTPUT);
	pinMode(hw->nss.pin, OUTPUT);
	SX1278_hw_SetNSS(hw, 1);
	digitalWrite(hw->reset.pin, HIGH);
}

__weak void SX1278_hw_SetNSS(SX1278_hw_t *hw, int value) {
	digitalWrite(hw->nss.pin, value);
}

__weak void SX1278_hw_Reset(SX1278_hw_t *hw) {
	SX1278_hw_SetNSS(hw, 1);
	digitalWrite(hw->reset.pin, LOW);

	SX1278_hw_DelayMs(1);

	digitalWrite(hw->reset.pin, HIGH);

	SX1278_hw_DelayMs(100);
}

__weak void SX1278_hw_SPICommand(SX1278_hw_t *hw, uint8_t cmd) {
	SX1278_hw_SetNSS(hw, 0);
	((SPIClass*)(hw->spi))->transfer(cmd);
}

__weak uint8_t SX1278_hw_SPIReadByte(SX1278_hw_t *hw) {
	uint8_t txByte = 0x00;
	uint8_t rxByte = 0x00;

	SX1278_hw_SetNSS(hw, 0);
	rxByte = ((SPIClass*)(hw->spi))->transfer(txByte);
	return rxByte;
}

__weak void SX1278_hw_DelayMs(uint32_t msec) {
	delay(msec);
}

__weak int SX1278_hw_GetDIO0(SX1278_hw_t *hw) {
	return digitalRead(hw->dio0.pin);
}

