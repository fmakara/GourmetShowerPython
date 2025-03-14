import rp2
from rp2 import PIO, StateMachine, asm_pio
from machine import Pin
# Implementado primariamente copiando o exemplo do RPI PICO
# https://github.com/raspberrypi/pico-examples/blob/master/pio/quadrature_encoder/quadrature_encoder.pio

@asm_pio(
    autopull=False,
    autopush=False,
    push_thresh=32,
    pull_thresh=32,
    out_shiftdir=rp2.PIO.SHIFT_RIGHT,
    in_shiftdir=rp2.PIO.SHIFT_LEFT,
    fifo_join=PIO.JOIN_NONE,
)
def quad_encoder_pio():
    # --------------------------------
    jmp('update')      # last 00 current 00
    jmp('decrement')   # last 00 current 01
    jmp('increment')   # last 00 current 10
    jmp('update')      # last 00 current 11Q
    # --------------------------------
    jmp('increment')   # last 01 current 00
    jmp('update')      # last 01 current 01
    jmp('update')      # last 01 current 10
    jmp('decrement')   # last 01 current 11
    # --------------------------------
    jmp('decrement')   # last 10 current 00
    jmp('update')      # last 10 current 01
    jmp('update')      # last 10 current 10
    jmp('increment')   # last 10 current 11
    # --------------------------------
    jmp('update')      # last 11 current 00
    jmp('increment')   # last 11 current 01
    label('decrement') # last 11 current 10
    jmp(y_dec, 'update')
    wrap_target()
    label('update')    # last 11 current 11
    mov(isr, y)
    push(noblock)                 # @ 16
    label('sample_pins')
    out(isr, 2)                   # @ 17
    in_(pins, 2)                  # @ 18
    mov(osr, isr)                 # @ 19
    mov(pc, isr)                  # @ 20
    label('increment')
    mov(y, invert(y))             # @ 21
    jmp(y_dec, 'increment_cont')  # @ 22
    label('increment_cont')
    mov(y, invert(y))             # @ 23
    wrap()
    nop() # @ 24 discovered with wokwi
    nop() # @ 25 that micropython puts
    nop() # @ 26 the program always at
    nop() # @ 27 the bottom.. so by
    nop() # @ 28 filling this space
    nop() # @ 29 it forces to be at
    nop() # @ 30 the top
    nop() # @ 31
    


class QuadEncoder():
    def __init__(self, swa=16, invert=False, sm=1):
        self._invert = invert
        self._swa = Pin(swa, Pin.IN, Pin.PULL_UP)
        self._swb = Pin(self._swa.id()+1, Pin.IN, Pin.PULL_UP)
        self._sm = StateMachine(sm, quad_encoder_pio, -1, in_base=self._swa, jmp_pin=self._swa)
        self._sm.exec(rp2.asm_pio_encode("set(y, 0)", 0))
        self._sm.active(1)
    
    def value(self):
        for i in range(self._sm.rx_fifo()):
            self._sm.get()
        data = self._sm.get()
        if data&0x8000_0000: data = data-0x1_0000_0000
        return -data if self._invert else data
