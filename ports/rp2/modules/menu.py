import time
from Graphics import Sprite

def days_in_month(year, month):
    dim = [31, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    if month==2:
        return 29 if (((year%4)==0) and ((year%100)!=0)) or (year%400)==0 else 28
    return dim[month] if month>=0 and month<=12 else 0

def fix_datetime(value):
    while value[5]>=60: value[5], value[4] = (value[5]-60, value[4]+1)
    while value[5]<0: value[5], value[4] = (value[5]+60, value[4]-1)
    while value[4]>=60: value[4], value[3] = (value[4]-60, value[3]+1)
    while value[4]<0: value[4], value[3] = (value[4]+60, value[3]-1)
    while value[3]>=24: value[3], value[2] = (value[3]-24, value[2]+1)
    while value[3]<0: value[3], value[2] = (value[3]+24, value[2]-1)
    while value[0]<2000 or value[0]>2200 or value[1]<1 or value[1]>12 or value[2]<1 or value[2]>days_in_month(value[0], value[1]):
        if value[0]<2000: value[0] = 2000
        if value[0]>2200: value[0] = 2200
        if value[1]>12: value[1], value[0] = (value[1]-12, value[0]+1)
        while value[1]<1: value[1], value[0] = (value[1]+12, value[0]-1)
        if value[2]>days_in_month(value[0], value[1]):
            value[2], value[1] = (value[2]-days_in_month(value[0], value[1]), value[1]+1)
            continue
        while value[2]<1:
            value[1] = value[1]-1
            value[2] = value[2]+days_in_month(value[0], value[1])
    return value

def print_center_x(text, x, y, font):
    size = font.calculateSize(text) # x, y, maxx
    font.print(text, x-size[2]//2, y)

class Menu():
    def __init__(self, display, default_font, btn_left, btn_right, btn_cancel, btn_ok):
        self._display = display
        self._w = display.width()
        self._h = display.height()
        self._def_font = default_font
        self._btn_left = btn_left
        self._btn_right = btn_right
        self._btn_cancel = btn_cancel
        self._btn_ok = btn_ok
        self._def_glyph_data = b'\x08\x08\x01\xc3\xe7~<<~\xe7\xc3'
        self._def_glyph = Sprite(raw=self._def_glyph_data)

    def horizontal_glyph_menu(self, items=[['item', None, None, None, None]], no_back=False, on_loop=None, first_field=0, preselected=None):
        internal=[] #                      Caption, Glyph, Callback, Params, Named Params
        for i in items:
            cap_size = self._def_font.calculateSize(i[0]) # x, y, maxx
            cap_y = self._display.height()-(cap_size[1]+self._def_font.lineHeight())
            glyph = self._def_glyph if (i[1] is None) else i[1]
            internal.append({
                'caption': i[0],
                'glyph': glyph,
                'callback': i[2],
                'cb_params': [] if i[3] is None else i[3],
                'cb_named_params': {} if i[4] is None else i[4],
                'cap_x': (self._display.width()-cap_size[2])//2,
                'cap_y': cap_y,
                'glyph_x': (self._display.width()-glyph.width())//2,
                'glyph_y': (cap_y-glyph.height())//2,
                })
        index = first_field%len(internal)
        last_input = 0
        def horizontal_glyph_menu_update_display(offset):
            self._display.clear()
            
            self._def_font.print('<', 0, self._h//2)
            self._def_font.print('>', self._w-5, self._h//2)
            if offset==0:
                self._display.copyFrom(internal[index]['glyph'], internal[index]['glyph_x'], internal[index]['glyph_y'])
                self._def_font.print(internal[index]['caption'], internal[index]['cap_x'], internal[index]['cap_y'])
            elif offset>0:
                other = (index+len(internal)-1)%len(internal)
                self._display.copyFrom(internal[index]['glyph'], internal[index]['glyph_x']+offset, internal[index]['glyph_y'])
                self._def_font.print(internal[index]['caption'], internal[index]['cap_x']+offset, internal[index]['cap_y'])
                self._display.copyFrom(internal[other]['glyph'], internal[other]['glyph_x']+offset-self._w, internal[other]['glyph_y'])
                self._def_font.print(internal[other]['caption'], internal[other]['cap_x']+offset-self._w, internal[other]['cap_y'])
            else:
                other = (index+1)%len(internal)
                self._display.copyFrom(internal[index]['glyph'], internal[index]['glyph_x']+offset, internal[index]['glyph_y'])
                self._def_font.print(internal[index]['caption'], internal[index]['cap_x']+offset, internal[index]['cap_y'])
                self._display.copyFrom(internal[other]['glyph'], internal[other]['glyph_x']+offset+self._w, internal[other]['glyph_y'])
                self._def_font.print(internal[other]['caption'], internal[other]['cap_x']+offset+self._w, internal[other]['cap_y'])
            
            if last_input==1: self._display.rect(0, -3+self._h//2, 5, self._def_font.height()+3+self._h//2, None)
            if last_input==2: self._display.rect(self._w-8, -3+self._h//2, self._w, self._def_font.height()+3+self._h//2, None)
            if last_input==3 and offset==0: self._display.rect(
                internal[index]['glyph_x']-3,
                internal[index]['glyph_y']-3,
                internal[index]['glyph_x']+3+internal[index]['glyph'].width(),
                internal[index]['glyph_y']+3+internal[index]['glyph'].height(),
                None)
            
            self._display.display()
        if preselected!=None:
            index = first_field%len(internal)
            if not internal[index]['callback'] is None:
                try:
                    internal[index]['callback'](*internal[index]['cb_params'],**internal[index]['cb_named_params'])
                except Exception as e:
                    self._display.clear()
                    self._def_font.print(repr(e),0,0)
                    print(repr(e))
                    self._display.display()
                    self.press_any()

        horizontal_glyph_menu_update_display(0)
        while True:
            curval = 0
            if not self._btn_left.value(): curval = 1 if curval==0 else 5
            if not self._btn_right.value(): curval = 2 if curval==0 else 5
            if not self._btn_ok.value(): curval = 3 if curval==0 else 5
            if not self._btn_cancel.value(): curval = 4 if curval==0 else 5
            
            if curval!=0:
                if (last_input==0 or curval==1 or curval==2)and last_input!=5:
                    if curval==1:
                        last_input = curval
                        for i in range(self._w//16, self._w, self._w//8):
                            horizontal_glyph_menu_update_display(i)
                            if not (on_loop is None): on_loop(val)
                        index = (index+len(internal)-1)%len(internal)
                    if curval==2:
                        last_input = curval
                        for i in range(self._w//16, self._w, self._w//8):
                            horizontal_glyph_menu_update_display(-i)
                            if not (on_loop is None): on_loop(val)
                        index = (index+1)%len(internal)
                    horizontal_glyph_menu_update_display(0)

                if last_input!=0 and last_input!=curval:
                    curval = 5
                
                last_input = curval
                horizontal_glyph_menu_update_display(0)
                
            elif last_input!=0:
                if last_input==4 and not no_back: return None
                if last_input==3:
                    if internal[index]['callback'] is None:
                        if not no_back: return index
                    else:
                        try:
                            internal[index]['callback'](*internal[index]['cb_params'],**internal[index]['cb_named_params'])
                        except Exception as e:
                            self._display.clear()
                            self._def_font.print(repr(e),0,0)
                            print(repr(e))
                            self._display.display()
                            self.press_any()
                last_input = curval
                horizontal_glyph_menu_update_display(0)

            last_input = curval
            time.sleep(0.05)
            if not (on_loop is None): on_loop(val)

    def press_any(self):
        anyPressed = False
        while not anyPressed:
            anyPressed = not (self._btn_left.value() and self._btn_right.value() and self._btn_ok.value() and self._btn_cancel.value())
            time.sleep(0.1)
        self._display.clear(None)
        self._display.display()
        while anyPressed:
            anyPressed = not (self._btn_left.value() and self._btn_right.value() and self._btn_ok.value() and self._btn_cancel.value())
            time.sleep(0.1)
        self._display.clear(None)
        self._display.display()

    def read_datetime(self, val=[2025,4,20,15,10,30], live=True, caption='data', on_loop=None, on_update=None, first_field=0):
        cap_size = self._def_font.calculateSize(caption) # x, y, maxx
        cap_off = (self._w-cap_size[2])//2
        val_top = cap_size[1]+self._def_font.height()
        arrow_y = val_top+((self._h-val_top)//2)-(self._def_font.height()//2)
        third_x = (self._w-10)//3
        third_centers = (5+third_x//2, 5+third_x+third_x//2, 5+third_x*2+third_x//2)
        half_y = (self._h-(cap_size[1]+self._def_font.lineHeight()))//2
        half_tops = (cap_size[1]+self._def_font.lineHeight(), cap_size[1]+self._def_font.lineHeight()+half_y)
        half_offs = (half_tops[0]+(half_y-self._def_font.height())//2, half_tops[1]+(half_y-self._def_font.height())//2)
        
        dateSig = lambda value: ((((((value[0]-2000)*12+value[1])*31+value[2])*24+value[3])*60+value[4])*60+value[5])
        lastSig = dateSig(val)
        currentSig = lastSig
        field = first_field
        last_input = 0
        def read_datetime_update_display():
            self._display.clear()
            self._def_font.print(caption, cap_off, 0)
            self._def_font.print('<', 0, arrow_y)
            self._def_font.print('>', self._w-5, arrow_y)
            self._def_font.print('/', third_centers[0]+third_x//2-2, half_offs[0])
            self._def_font.print('/', third_centers[1]+third_x//2-2, half_offs[0])
            if not live or (time.ticks_ms()%1000)<500:
                self._def_font.print(':', third_centers[0]+third_x//2-2, half_offs[1])
                self._def_font.print(':', third_centers[1]+third_x//2-2, half_offs[1])
            print_center_x(str(val[0]), third_centers[2], half_offs[0], self._def_font)
            print_center_x(f"{val[1]:02d}", third_centers[1], half_offs[0], self._def_font)
            print_center_x(f"{val[2]:02d}", third_centers[0], half_offs[0], self._def_font)
            
            print_center_x(f"{val[3]:02d}", third_centers[0], half_offs[1], self._def_font)
            print_center_x(f"{val[4]:02d}", third_centers[1], half_offs[1], self._def_font)
            print_center_x(f"{val[5]:02d}", third_centers[2], half_offs[1], self._def_font)
            self._display.rect(third_centers[field%3]+4-third_x//2, half_tops[field//3], third_centers[field%3]-4+third_x//2, half_tops[field//3]+half_y, None)
            if last_input==1: self._display.rect(0, arrow_y-3, 5, arrow_y+self._def_font.height()+3, None)
            if last_input==2: self._display.rect(self._w-8, arrow_y-3, self._w, arrow_y+self._def_font.height()+3, None)
            self._display.display()
        
        val = fix_datetime(val)
        prev_update = (time.ticks_ms()%1000)>500
        read_datetime_update_display()
        last_second_update = time.ticks_ms()//1000
        
        currentSig = dateSig(val)
        if lastSig!=currentSig and not (on_update is None):
            on_update(val)
        lastSig = currentSig
        
        while True:
            curval = 0
            if not self._btn_left.value(): curval = 1 if curval==0 else 5
            if not self._btn_right.value(): curval = 2 if curval==0 else 5
            if not self._btn_ok.value(): curval = 3 if curval==0 else 5
            if not self._btn_cancel.value(): curval = 4 if curval==0 else 5
            
            if curval!=0:
                if last_input==0 or (last_input!=5 and repeat_avoid<time.ticks_ms()):
                    if last_input==0:
                        repeat_avoid = time.ticks_ms()+1000
                    fixfield = field if field>2 else 2-field
                    if curval==1: val[fixfield] = val[fixfield]-1
                    if curval==2: val[fixfield] = val[fixfield]+1
                    val = fix_datetime(val)
                    currentSig = dateSig(val)
                    
                    if lastSig!=currentSig and not (on_update is None):
                        on_update(val)
                    lastSig = currentSig

                elif last_input!=curval:
                    curval = 5
                
                last_input = curval
                read_datetime_update_display()
            elif last_input!=0:
                if last_input==4:
                    if field==0: return None
                    field = field-1
                if last_input==3:
                    if field==5: return val
                    field = field+1
                last_input = curval
                read_datetime_update_display()
            
            current_update = (time.ticks_ms()%1000)>500
            if current_update!=prev_update:
                if live:
                    second = time.ticks_ms()//1000
                    val[5] = val[5]+(second-last_second_update)
                    val = fix_datetime(val)
                    currentSig = dateSig(val)
                    
                    if lastSig!=currentSig and not (on_update is None):
                        on_update(val)
                    lastSig = currentSig
                    
                    last_second_update = second
                read_datetime_update_display()
            prev_update = current_update
        
            time.sleep(0.05)
            if not (on_loop is None): on_loop(val)

    def read_value(self, val=0, min_val=-2147483647, max_val=2147483647, increment=1, display_mult=1, formato="{}", caption='value', val_font=None, on_loop=None, on_update=None):
        if val_font is None: val_font = self._def_font
        cap_size = self._def_font.calculateSize(caption) # x, y, maxx
        cap_off = (self._w-cap_size[2])//2
        val_top = cap_size[1]+self._def_font.height()
        arrow_y = val_top+((self._h-val_top)//2)-(self._def_font.height()//2)
        val_y = val_top+((self._h-val_top)//2)-(val_font.height()//2)
        last_input = 0
        repeat_avoid = 0
        last_val = val
        
        def read_value_update_display():
            self._display.clear()
            self._def_font.print(caption, cap_off, 0)
            self._def_font.print('<', 0, arrow_y)
            self._def_font.print('>', self._w-5, arrow_y)
            val_str = formato.format(val*display_mult)
            val_size = val_font.calculateSize(val_str)
            val_font.print(val_str, self._h-(val_size[2]//2), val_y)
            if last_input==1: self._display.rect(0, arrow_y-3, 5, arrow_y+self._def_font.height()+3, None)
            if last_input==2: self._display.rect(self._w-8, arrow_y-3, self._w, arrow_y+self._def_font.height()+3, None)
            if last_input==3: self._display.rect(self._h-(val_size[2]//2)-3, val_y-3, self._h+(val_size[2]//2)+3, val_y+val_font.height()+3, None)
            if last_input==4:
                self._display.line(self._h-(val_size[2]//2)-5, val_y-5, self._h+(val_size[2]//2)+5, val_y+val_font.height()+5, True)
                self._display.line(self._h-(val_size[2]//2)-5, val_y+val_font.height()+5, self._h+(val_size[2]//2)+5, val_y-5, True)
            self._display.display()
        
        if val<min_val:
            val = min_val
        if val>max_val:
            val = max_val
        read_value_update_display()
        
        if last_val!=val and not (on_update is None):
            on_update(val)
        last_val = val

        while True:
            curval = 0
            if not self._btn_left.value(): curval = 1 if curval==0 else 5
            if not self._btn_right.value(): curval = 2 if curval==0 else 5
            if not self._btn_ok.value(): curval = 3 if curval==0 else 5
            if not self._btn_cancel.value(): curval = 4 if curval==0 else 5
            
            if curval!=0:
                if last_input==0 or (last_input!=5 and repeat_avoid<time.ticks_ms()):
                    if last_input==0:
                        repeat_avoid = time.ticks_ms()+1000
                    if curval==1:
                        val = val-increment
                        if val<min_val:
                            val = min_val
                    if curval==2:
                        val = val+increment
                        if val>max_val:
                            val = max_val
                    
                    if last_val!=val and not (on_update is None):
                        on_update(val)
                    last_val = val

                elif last_input!=curval:
                    curval = 5
                
                last_input = curval
                read_value_update_display()
            elif last_input!=0:
                if last_input==4: return None
                if last_input==3: return val
                last_input = curval
                read_value_update_display()
            
            time.sleep(0.05)
            if not (on_loop is None): on_loop(val)

