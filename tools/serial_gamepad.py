#!/usr/bin/env python3
"""Windows keyboard -> Tufty NES CDC1. Linux supports --buttons for USB checks."""
from __future__ import annotations
import argparse
import os
import secrets
import time

VID, PID = 0xCAFE, 0x4025
A, B, SELECT, START, UP, DOWN, LEFT, RIGHT = (1 << n for n in range(8))

def packet(controller_id, sequence, buttons, version=2):
    if not 1 <= controller_id <= 65535 or version not in (1, 2):
        raise ValueError('invalid controller/version')
    if not 0 <= buttons <= (31 if version == 1 else 255):
        raise ValueError('invalid buttons')
    data=b'TGP'+bytes((version,controller_id&255,controller_id>>8,sequence&255,buttons))
    return b'#TGP:'+data.hex().encode()+b'\n'

class Sender:
    def __init__(self, connection, controller_id=1, version=2, clock=time.monotonic):
        self.connection=connection
        self.controller_id=controller_id
        self.version=version
        self.clock=clock
        self.ready=False
        self.line=bytearray()
        self.overflow=False
        self.sequence=0
        self.buttons=None
        self.last_sent=self.last_alive=clock()
        self.nonce=b''
        self.begin()

    def write(self, data):
        if self.connection.write(data)!=len(data): raise OSError('short serial write')

    def begin(self):
        self.nonce=secrets.token_hex(8).encode()
        self.ready=False
        self.sequence=0
        self.buttons=None
        self.last_hello=self.clock()
        self.write(b'#TGP:HELLO:'+f'{self.controller_id:04x}'.encode()+b':'+self.nonce+b'\n')

    def poll(self):
        for value in self.connection.read(256):
            if value==10:
                if not self.overflow:
                    line=bytes(self.line).rstrip(b'\r')
                    if line==b'#TGP:APP' or line==b'#TGP:BYE:'+self.nonce: self.begin()
                    elif line==b'#TGP:READY:'+self.nonce:
                        self.ready=True
                        self.last_alive=self.clock()
                    elif self.ready and line==b'#TGP:ALIVE:'+self.nonce:
                        self.last_alive=self.clock()
                self.line.clear()
                self.overflow=False
            elif not self.overflow:
                if len(self.line)<128: self.line.append(value)
                else: self.overflow=True
        if self.ready and self.clock()-self.last_alive>=1.2: self.begin()
        elif not self.ready and self.clock()-self.last_hello>=1.2: self.begin()

    def send(self, buttons):
        if not self.ready: return
        now=self.clock()
        if buttons!=self.buttons or now-self.last_sent>=0.12:
            self.write(packet(self.controller_id,self.sequence,buttons,self.version))
            self.sequence=(self.sequence+1)&255
            self.buttons=buttons
            self.last_sent=now

    def menu(self):
        if self.ready: self.write(b'#TGP:MENU:'+self.nonce+b'\n')

    def close(self):
        if self.ready:
            try:
                self.write(packet(self.controller_id,self.sequence,0,self.version))
                self.write(b'#TGP:END:'+self.nonce+b'\n')
            except OSError: pass
        self.ready=False

def ports():
    from serial.tools import list_ports
    candidates=[p for p in list_ports.comports() if (p.vid,p.pid)==(VID,PID)]
    result=[]
    for p in candidates:
        interface=(p.interface or '').lower()
        hwid=(p.hwid or '').lower()
        location=(p.location or '').lower()
        if 'console' in interface or 'mi_00' in hwid or location.endswith(':1.0'): continue
        result.append(p)
    return result

def choose_port(requested):
    if requested: return requested
    found=ports()
    if len(found)==1: return found[0].device
    if not found: return None
    print('CDC ports:')
    for i,p in enumerate(found): print(f'  {i+1}. {p.device} {p.interface or p.description}')
    return found[int(input('Choose Gamepad / CDC1: '))-1].device

def keyboard(version,get):
    down=lambda key: bool(get(key)&0x8000)
    if version==1:
        keys=((0x26,1),(ord('W'),1),(0x28,2),(ord('S'),2),
              (0x25,4),(ord('A'),4),(0x27,8),(ord('D'),8),
              (0x20,16),(0x0d,16),(ord('B'),16))
    else:
        keys=((0x26,UP),(ord('W'),UP),(0x28,DOWN),(ord('S'),DOWN),
              (0x25,LEFT),(ord('A'),LEFT),(0x27,RIGHT),(ord('D'),RIGHT),
              (ord('Z'),A),(0x20,A),(ord('X'),B),(0x0d,START),(0xa1,SELECT))
    bits=0
    for key,bit in keys:
        if down(key): bits|=bit
    return bits,down(0x1b)

class FocusedKeyboard:
    """Read gamepad keys only while the launching terminal is foreground."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        user32=ctypes.windll.user32
        kernel32=ctypes.windll.kernel32
        # HWND is pointer-sized, including on 64-bit Windows.
        kernel32.GetConsoleWindow.argtypes=[]
        kernel32.GetConsoleWindow.restype=wintypes.HWND
        user32.GetForegroundWindow.argtypes=[]
        user32.GetForegroundWindow.restype=wintypes.HWND
        user32.IsWindowVisible.argtypes=[wintypes.HWND]
        user32.IsWindowVisible.restype=wintypes.BOOL
        user32.GetAsyncKeyState.argtypes=[ctypes.c_int]
        user32.GetAsyncKeyState.restype=ctypes.c_short
        self.foreground=user32.GetForegroundWindow
        self.get=user32.GetAsyncKeyState
        self.window=kernel32.GetConsoleWindow()
        # Windows Terminal uses an invisible pseudoconsole window. Capture its
        # foreground terminal once at startup, never the newly focused app.
        if not self.window or not user32.IsWindowVisible(self.window):
            self.window=self.foreground()
        if not self.window:
            raise OSError('no terminal window; start this script in an active terminal')

    def read(self,version):
        if self.foreground()!=self.window: return 0,False
        state=keyboard(version,self.get)
        # Discard a sample if focus changed while querying the keys.
        return state if self.foreground()==self.window else (0,False)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',help='CDC1 COM port or /dev/ttyACM1')
    p.add_argument('--controller-id',type=int,default=1)
    p.add_argument('--v1',action='store_true')
    p.add_argument('--buttons',type=lambda s:int(s,0),help='fixed bitmap for USB checks')
    p.add_argument('--seconds',type=float,help='stop after this many seconds')
    args=p.parse_args()
    version=1 if args.v1 else 2
    try: packet(args.controller_id,0,args.buttons or 0,version)
    except ValueError as e: p.error(str(e))
    if args.buttons is None and os.name!='nt': p.error('keyboard capture uses Windows; use --buttons on Linux')
    try: import serial
    except ImportError: p.error('install dependency: python -m pip install pyserial')
    try: capture=FocusedKeyboard() if args.buttons is None else None
    except OSError as e: p.error(str(e))
    connection=None
    sender=None
    started=time.monotonic()
    previous_menu=False
    print('Arrows/WASD: move; Z/Space: A; X: B; Enter: Start; Right Shift: Select; Esc: library; Ctrl+C: quit')
    if capture: print('Keyboard active only in this terminal window; switching windows releases all buttons.')
    try:
        while args.seconds is None or time.monotonic()-started<args.seconds:
            try:
                if connection is None:
                    name=choose_port(args.port)
                    if name is None:
                        time.sleep(0.5)
                        continue
                    connection=serial.Serial(name,115200,timeout=0,write_timeout=0.25)
                    sender=Sender(connection,args.controller_id,version)
                    print(f'{name}: connected, waiting for READY')
                was_ready=sender.ready
                sender.poll()
                if sender.ready and not was_ready: print('READY')
                bits,menu=(args.buttons,False) if args.buttons is not None else capture.read(version)
                sender.send(bits)
                if menu and not previous_menu: sender.menu()
                previous_menu=menu
                time.sleep(0.005)
            except (OSError,serial.SerialException) as e:
                print(f'USB: {e}; waiting for reconnect')
                if connection: connection.close()
                connection=sender=None
                time.sleep(0.5)
    except KeyboardInterrupt: pass
    finally:
        if sender: sender.close()
        if connection: connection.close()

if __name__=='__main__': main()
