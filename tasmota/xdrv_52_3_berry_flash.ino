/*
  xdrv_52_3_berry_webserver.ino - Berry scripting language, webserver module

  Copyright (C) 2021 Stephan Hadinger, Berry language by Guan Wenliang https://github.com/Skiars/berry

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


#ifdef USE_BERRY

#include <berry.h>
#include "esp_spi_flash.h"

size_t FlashWriteSubSector(uint32_t address_start, const uint8_t *data, size_t size) {
    uint32_t addr = address_start;
    size_t size_left = size;
    size_t current_offset = 0;
    esp_err_t ret;
    // Memory is unaligned, so we need to copy it to an aligned buffer
    uint8_t buffer[SPI_FLASH_SEC_SIZE] __attribute__((aligned(4)));

    while (size_left) {
      uint32_t page_addr = addr & ~(SPI_FLASH_SEC_SIZE - 1);
      uint32_t addr_in_page = addr & (SPI_FLASH_SEC_SIZE - 1);
      uint32_t size_in_page = size_left;
      if (addr_in_page + size_in_page > SPI_FLASH_SEC_SIZE) {
        size_in_page = SPI_FLASH_SEC_SIZE - addr_in_page;
      }

      // AddLog(LOG_LEVEL_DEBUG, ">>>: flash_write addr=%p size=%i -- page_addr=%p addr_in_page=%p size_in_page=%i size_left=%i", address_start, size, page_addr, addr_in_page, size_in_page, size_left);
      // check if whole page?
      if (addr_in_page == 0 && size_in_page == SPI_FLASH_SEC_SIZE) {
        memcpy(buffer, data + current_offset, SPI_FLASH_SEC_SIZE);
      } else {
        ret = spi_flash_read(page_addr, buffer, SPI_FLASH_SEC_SIZE);
        if (ret) { AddLog(LOG_LEVEL_INFO, "BRY: could not read flash %p (0x%X)", page_addr, SPI_FLASH_SEC_SIZE); return 0; }
        memcpy(buffer + addr_in_page, data + current_offset, size_in_page);
      }
      ret = spi_flash_erase_sector(page_addr / SPI_FLASH_SEC_SIZE);
      if (ret) { AddLog(LOG_LEVEL_INFO, "BRY: could not erase flash sector 0x%X", page_addr / SPI_FLASH_SEC_SIZE); return 0; }
      spi_flash_write(page_addr, buffer, SPI_FLASH_SEC_SIZE);
      if (ret) { AddLog(LOG_LEVEL_INFO, "BRY: could not write flash %p (0x%X)", page_addr, SPI_FLASH_SEC_SIZE); return 0; }

      addr += size_in_page;
      current_offset += size_in_page;
      size_left -= size_in_page;
    }

    return current_offset;
}

/*********************************************************************************************\
 * Native functions mapped to Berry functions
 * 
 * import flash
 * 
\*********************************************************************************************/
extern "C" {
  // Berry: `flash.read(address:int[, length:int]) -> bytes()`
  //
  // If length is not specified, it is full block 4KB
  int32_t p_flash_read(struct bvm *vm);
  int32_t p_flash_read(struct bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc >= 1 && be_isint(vm, 1) && 
        (argc < 2 || be_isint(vm, 2)) ) {    // optional second argument must be int
      uint32_t address = be_toint(vm, 1);
      uint32_t length = 0x1000;
      if (argc >= 2) {
        length = be_toint(vm, 2);
        if (length <= 0) { length = 0x1000; }
      }
      // allocate a buffer in the heap that will be automatically freed when going out of scope
      auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[length]);
      esp_err_t ret = spi_flash_read(address, buf.get(), length);
      if (ret)  {
        be_raise(vm, "internal_error", "Error calling spi_flash_read()");
      }
      be_pushbytes(vm, buf.get(), length);
      be_return(vm);
    }
    be_raise(vm, kTypeError, nullptr);
  }

  // Berry: `flash.write(address:int, content:bytes() [, no_erase:bool]) -> nil`
  // if `no_erase` is true, just call spi_flash_write
  int32_t p_flash_write(struct bvm *vm);
  int32_t p_flash_write(struct bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc >= 2 && be_isint(vm, 1) && be_isinstance(vm, 2)) {
      be_getglobal(vm, "bytes"); /* get the bytes class */ /* TODO eventually replace with be_getbuiltin */
      if (be_isderived(vm, 2)) {
        uint32_t address = be_toint(vm, 1);
        size_t length = 0;
        const void * bytes = be_tobytes(vm, 2, &length);
        if (bytes && length > 0) {
          size_t ret = FlashWriteSubSector(address, (const uint8_t*)bytes, length);
          if (ret == 0)  {
            be_raise(vm, "internal_error", "Error calling spi_flash_write()");
          }
          be_return_nil(vm);
          // success
        }
      }
    }
    be_raise(vm, kTypeError, nullptr);
  }

  // Berry: `flash.erase(address:int, length:int) -> nil`
  //
  // Address and length must be 4KB aligned
  int32_t p_flash_erase(struct bvm *vm);
  int32_t p_flash_erase(struct bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc >= 2 && be_isint(vm, 1) && be_isint(vm, 2)) {
      int32_t address = be_toint(vm, 1);
      int32_t length = be_toint(vm, 2);
      if ((address % 0x1000) != 0 || address < 0) {
        be_raise(vm, "value_error", "Address must be a multiple of 0x1000");
      }
      if ((length % 0x1000) != 0 || length < 0) {
        be_raise(vm, "value_error", "Length must be a multiple of 0x1000");
      }
      esp_err_t ret = spi_flash_erase_range(address, length);
      be_return_nil(vm);
    }
    be_raise(vm, kTypeError, nullptr);
  }

}

#endif  // USE_BERRY
