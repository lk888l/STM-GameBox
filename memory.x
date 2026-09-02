/*
 * STM32F103C8T6: 64 KiB Flash, 20 KiB SRAM.
 *
 * The final two 1 KiB Flash pages are deliberately excluded from the linker
 * region and owned by the settings journal. Keep this file in sync with
 * firmware/src/platform/storage.rs.
 */
MEMORY
{
  FLASH : ORIGIN = 0x08000000, LENGTH = 62K
  RAM   : ORIGIN = 0x20000000, LENGTH = 20K
}

