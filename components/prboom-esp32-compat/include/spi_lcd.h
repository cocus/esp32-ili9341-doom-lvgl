#ifndef I80_LCD_H
#define I80_LCD_H

void spi_lcd_wait_finish();
void spi_lcd_send(uint16_t *scr);
void spi_lcd_init();

#endif // I80_LCD_H
