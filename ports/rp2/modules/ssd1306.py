from Graphics import Sprite
from machine import Pin, I2C


SSD1306_COMMAND_HEADER = 0x00  # See datasheet
SSD1306_DATA_HEADER = 0x40  # See datasheet

SSD1306_MEMORYMODE = 0x20  # See datasheet
SSD1306_COLUMNADDR = 0x21  # See datasheet
SSD1306_PAGEADDR = 0x22  # See datasheet
SSD1306_SETCONTRAST = 0x81  # See datasheet
SSD1306_CHARGEPUMP = 0x8D  # See datasheet
SSD1306_SEGREMAP = 0xA0  # See datasheet
SSD1306_DISPLAYALLON_RESUME = 0xA4  # See datasheet
SSD1306_DISPLAYALLON = 0xA5  # Not currently used
SSD1306_NORMALDISPLAY = 0xA6  # See datasheet
SSD1306_INVERTDISPLAY = 0xA7  # See datasheet
SSD1306_SETMULTIPLEX = 0xA8  # See datasheet
SSD1306_DISPLAYOFF = 0xAE  # See datasheet
SSD1306_DISPLAYON = 0xAF  # See datasheet
SSD1306_COMSCANINC = 0xC0  # Not currently used
SSD1306_COMSCANDEC = 0xC8  # See datasheet
SSD1306_SETDISPLAYOFFSET = 0xD3  # See datasheet
SSD1306_SETDISPLAYCLOCKDIV = 0xD5  # See datasheet
SSD1306_SETPRECHARGE = 0xD9  # See datasheet
SSD1306_SETCOMPINS = 0xDA  # See datasheet
SSD1306_SETVCOMDETECT = 0xDB  # See datasheet

SSD1306_SETLOWCOLUMN = 0x00  # Not currently used
SSD1306_SETHIGHCOLUMN = 0x10  # Not currently used
SSD1306_SETSTARTLINE = 0x40  # See datasheet

SSD1306_EXTERNALVCC = 0x01  # External display voltage source
SSD1306_SWITCHCAPVCC = 0x02  # Gen. display voltage from 3.3V

SSD1306_RIGHT_HORIZONTAL_SCROLL = 0x26  # Init rt scroll
SSD1306_LEFT_HORIZONTAL_SCROLL = 0x27  # Init left scroll
SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL = 0x29  # Init diag scroll
SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL = 0x2A  # Init diag scroll
SSD1306_DEACTIVATE_SCROLL = 0x2E  # Stop scroll
SSD1306_ACTIVATE_SCROLL = 0x2F  # Start scroll
SSD1306_SET_VERTICAL_SCROLL_AREA = 0xA3  # Set scroll range

SSD1306_init1 = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_DISPLAYOFF,
    SSD1306_SETDISPLAYCLOCKDIV,
    0x80,                           # the suggested ratio 0x80
    SSD1306_SETMULTIPLEX])
SSD1306_init2 = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_SETDISPLAYOFFSET,
    0x0,                            # no offset
    SSD1306_SETSTARTLINE | 0x0,     # line #0
    SSD1306_CHARGEPUMP])
SSD1306_init3 = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_MEMORYMODE,
    0x01,                           # 0x0 Vertical adressing
    SSD1306_SEGREMAP | 0x1,
    0xC0])                          # was SSD1306_COMSCANDEC
SSD1306_init4a = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_SETCOMPINS,
    SSD1306_SWITCHCAPVCC|0x10,
    SSD1306_SETCONTRAST,
    0xCF])
SSD1306_init5 = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_SETVCOMDETECT,
    0x40,
    SSD1306_DISPLAYALLON_RESUME,
    SSD1306_NORMALDISPLAY,
    SSD1306_DEACTIVATE_SCROLL,
    SSD1306_DISPLAYON])             # Main screen turn on
SSD1306_dlist1 = bytes([
    SSD1306_COMMAND_HEADER,
    SSD1306_PAGEADDR,
    0,                              # Page start address
    7,                              # end
    SSD1306_COLUMNADDR,             # Col start and end
    0,
    127])


class SSD1306(Sprite):
    def __init__(self, scl=Pin(27), sda=Pin(26), i2cmod=1, address=60, startup=True, rotation=False):
        super().__init__(width=128, height=64, stride=8, buffer=bytes(8*128))
        self.i2c = I2C(i2cmod, sda=sda, scl=scl, freq=400_000)
        self.addr = address
        self._rotation = rotation
        if startup:
            self.init()
            self.display()

    def rotation(self, rotation):
        self._rotation = rotation

    def init(self):
        self.i2c.writeto(self.addr, SSD1306_init1)
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, self.height()-1]))
        self.i2c.writeto(self.addr, SSD1306_init2)
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, SSD1306_SETHIGHCOLUMN|4]))
        self.i2c.writeto(self.addr, SSD1306_init3)
        self.i2c.writeto(self.addr, SSD1306_init4a)
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, SSD1306_SETPRECHARGE]))
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, 0xF1]))
        self.i2c.writeto(self.addr, SSD1306_init5)

    def display(self):
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, SSD1306_COMSCANDEC if self._rotation else SSD1306_COMSCANINC]))
        self.i2c.writeto(self.addr, bytes([SSD1306_COMMAND_HEADER, SSD1306_SEGREMAP | (1 if self._rotation else 0)]))
        self.i2c.writeto(self.addr, SSD1306_dlist1)
        self.i2c.writeto(self.addr, bytes([SSD1306_DATA_HEADER])+self.buffer())

