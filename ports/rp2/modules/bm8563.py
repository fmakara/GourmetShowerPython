from machine import Pin, I2C, RTC

class BM8563():
    def __init__(self, scl=Pin(21), sda=Pin(20), i2cmod=0, address=81, init=True):
        self.i2c = I2C(i2cmod, sda=sda, scl=scl, freq=400_000)
        self._addr = address
        self.rtc = RTC()
        self.trusted = False
        if init:
            self.initFromRtc()

    def _bcd2int(self, bcd):
        return (bcd&0xF)+(((bcd>>4)&0xF)*10)

    def _int2bcd(self, i):
        return ((i//10)<<4)|(i%10)

    def _daysInMonth(self, year, month):
        dim = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
        if month!=2: return dim[month-1]
        if (year%4)==0 and ( (year%100)!=0 or (year%400)==0 ): return 29
        return 28

    def now(self):
        now = self.rtc.datetime()
        return now[0:3]+now[4:7]

    def setRtc(self, datetime):
        if datetime is None:
            datetime = self.now()
        if len(datetime)==8:
            datetime = datetime[0:3]+datetime[4:7]
        if len(datetime)!=6:
            raise(Exception('datetime must have 6 fields'))
        if datetime[0]<2000 or datetime[0]>2200:
            raise(Exception('invalid year'))
        if datetime[1]<1 or datetime[1]>12:
            raise(Exception('invalid month'))
        if datetime[2]<1 or datetime[2]>self._daysInMonth(datetime[0], datetime[1]):
            raise(Exception('invalid day'))
        if datetime[3]<0 or datetime[3]>23:
            raise(Exception('invalid hour'))
        if datetime[4]<0 or datetime[4]>59:
            raise(Exception('invalid minute'))
        if datetime[5]<0 or datetime[5]>59:
            raise(Exception('invalid second'))

        century = 0x80 if datetime[0]>2099 else 0
        time = bytes([
            self._int2bcd(datetime[5]),
            self._int2bcd(datetime[4]),
            self._int2bcd(datetime[3]),
            self._int2bcd(datetime[2]),
            self.rtc.weekday(datetime[0], datetime[1], datetime[2])
        ])
        months = bytes([
            self._int2bcd(datetime[1]),
            self._int2bcd(datetime[0]%100)|century
        ])
        self.i2c.writeto_mem(self._addr, 0x7, months)
        self.i2c.writeto_mem(self._addr, 0x2, time)
        self.trusted = True

    def readTimeFromRtc(self):
        self.i2c.writeto_mem(self._addr, 0x0, b'\x00\x00')
        self.i2c.writeto_mem(self._addr, 0xD, b'\x03\x03')
        b = self.i2c.readfrom_mem(self._addr, 0x2, 7)
        century = (b[6]&0x80)!=0
        self.trusted = (b[0]&0x80)==0
        return (
            (2100 if century else 2000)+self._bcd2int(b[6]),
            self._bcd2int(b[5]&0x1F),
            self._bcd2int(b[3]&0x3F),
            self._bcd2int(b[4]&0x07),
            self._bcd2int(b[2]&0x3F),
            self._bcd2int(b[1]&0x7F),
            self._bcd2int(b[0]&0x7F),
            0)

    def initFromRtc(self):
        self.rtc.datetime(self.readTimeFromRtc())

    def readAlarmRam(self):
        b = self.i2c.readfrom_mem(self._addr, 0x9, 4)
        c = self.i2c.readfrom_mem(self._addr, 0xF, 1)
        c = c[0]-1
        m = b[0]
        h = ((b[1]>>1)&0x40)|(b[1]&0x3F)
        d = ((b[2]>>1)&0x40)|(b[2]&0x3F)
        w = ((b[3]>>4)&0x08)|(b[3]&0x07)
        return (m<<0)|(h<<8)|(d<<15)|(w<<22)|(c<<26)

    def writeAlarmRam(self, i34b):
        if not(type(i34b) is int):
            raise(Exception('i32b must be int'))
        m = (i34b>>0)&0xFF
        h = (i34b>>8)&0x7F
        d = (i34b>>15)&0x7F
        w = (i34b>>22)&0x0F
        c = ((i34b>>26)+2)&0xFF
        h = ((h<<1)&0x80)|(h&0x3F)
        d = ((d<<1)&0x80)|(d&0x3F)
        w = ((w<<4)&0x80)|(w&0x07)
        self.i2c.writeto_mem(self._addr, 0xF, bytes([c]))
        self.i2c.writeto_mem(self._addr, 0x9, bytes([m, h, d, w]))
