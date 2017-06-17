#include "eeprom.h"

// global objetcs

static AT24C32 _eeprom;

/*! \brief  Read byte from EEPROM. */
unsigned char eeprom_get_char(unsigned int addr)
{
	return _eeprom.read(addr);
}

/*! \brief  Write byte to EEPROM. */
void eeprom_put_char(unsigned int addr, unsigned char new_value)
{
	_eeprom.write(addr, new_value);
}

void memcpy_to_eeprom_with_checksum(unsigned int destination, char *source, unsigned int size) {
	_eeprom.writeChars(destination, source, size);
}

int memcpy_from_eeprom_with_checksum(char *destination, unsigned int source, unsigned int size) {
	_eeprom.readChars(source, destination, size);
	return 1;
}
