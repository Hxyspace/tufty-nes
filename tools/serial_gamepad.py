#!/usr/bin/env python3
"""Windows keyboard -> Tufty NES CDC1. Linux supports --buttons for USB checks."""
from __future__ import annotations
import argparse
import os
import secrets
import time

VID, PID = 0xCAFE, 0x4025
A, B, SELECT, START, UP, DOWN, LEFT, RIGHT = (1 << n for n in range(8))

NEUTRAL_REPORT = bytes.fromhex('80 80 80 80 FF 00 00 00 00 00')
DISCONNECT_REPORT = bytes.fromhex('54 47 50 01 42 59 45 00 00 A5')

def make_report(buttons=0, home=False):
    """Map NES keyboard buttons to the same ten-byte payload used by BLE."""
    if not isinstance(buttons,int) or not 0 <= buttons <= 255:
        raise ValueError('buttons must be an NES bitmap from 0 to 255')
    x=bool(buttons&RIGHT)-bool(buttons&LEFT)
    y=bool(buttons&DOWN)-bool(buttons&UP)
    hat={(0,-1):0,(1,-1):1,(1,0):2,(1,1):3,(0,1):4,
         (-1,1):5,(-1,0):6,(-1,-1):7,(0,0):255}[x,y]
    return bytes((128,128,128,128,hat,buttons&3,(buttons&12)|(16 if home else 0),0,0,0))

def packet(controller_id, sequence, report):
    if not isinstance(controller_id,int) or not 1 <= controller_id <= 65535:
        raise ValueError('controller ID must be from 1 to 65535')
    if not isinstance(report,(bytes,bytearray)) or len(report)!=10:
        raise ValueError('report must contain exactly ten bytes')
    data=b'TGP'+bytes((3,controller_id&255,controller_id>>8,sequence&255))+report
    return b'#TGP:'+data.hex().encode()+b'\n'

class Sender:
    def __init__(self, connection, controller_id=1, clock=time.monotonic):
        self.connection=connection
        self.controller_id=controller_id
        self.clock=clock
        self.ready=self.closed=False
        self.line=bytearray()
        self.overflow=False
        self.sequence=0
        self.report=None
        self.last_sent=self.last_alive=clock()
        self.nonce=b''
        packet(controller_id,0,NEUTRAL_REPORT)
        self.begin()

    def write(self, data):
        if self.connection.write(data)!=len(data): raise OSError('short serial write')

    def begin(self):
        if self.closed: return
        self.nonce=secrets.token_hex(8).encode()
        self.ready=False
        self.sequence=0
        self.report=None
        self.last_hello=self.clock()
        self.write(b'#TGP:HELLO:'+f'{self.controller_id:04x}'.encode()+b':'+self.nonce+b':3\n')

    def poll(self):
        for value in self.connection.read(256):
            if value==10:
                if not self.overflow:
                    line=bytes(self.line).rstrip(b'\r')
                    if line==b'#TGP:BYE:'+self.nonce:
                        self.ready=False
                        self.begin()
                    elif not self.closed:
                        if line==b'#TGP:APP': self.begin()
                        elif line==b'#TGP:READY:'+self.nonce+b':3':
                            self.ready=True
                            self.last_alive=self.clock()
                        elif self.ready and line==b'#TGP:ALIVE:'+self.nonce:
                            self.last_alive=self.clock()
                self.line.clear()
                self.overflow=False
            elif not self.overflow:
                if len(self.line)<128: self.line.append(value)
                else: self.overflow=True
        if self.closed: return
        if self.ready and self.clock()-self.last_alive>=1.2: self.begin()
        elif not self.ready and self.clock()-self.last_hello>=1.2: self.begin()

    def send(self, report):
        if not self.ready or self.closed: return
        now=self.clock()
        if report!=self.report or now-self.last_sent>=0.12:
            self.write(packet(self.controller_id,self.sequence,report))
            self.sequence=(self.sequence+1)&255
            self.report=bytes(report)
            self.last_sent=now

    def close(self, timeout=0.25):
        if self.closed: return
        self.closed=True
        try:
            if self.ready:
                for report in (NEUTRAL_REPORT,DISCONNECT_REPORT):
                    self.write(packet(self.controller_id,self.sequence,report))
                    self.sequence=(self.sequence+1)&255
                deadline=time.monotonic()+timeout
                while self.ready and time.monotonic()<deadline:
                    self.poll()
                    if self.ready: time.sleep(0.005)
        except OSError:
            pass
        finally:
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

def keyboard(get):
    down=lambda key: bool(get(key)&0x8000)
    keys=((0x26,UP),(ord('W'),UP),(0x28,DOWN),(ord('S'),DOWN),
          (0x25,LEFT),(ord('A'),LEFT),(0x27,RIGHT),(ord('D'),RIGHT),
          (ord('Z'),A),(0x20,A),(ord('X'),B),(0x0d,START),(0xa1,SELECT))
    bits=0
    for key,bit in keys:
        if down(key): bits|=bit
    return make_report(bits,home=down(0x1b))

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

    def read(self):
        if self.foreground()!=self.window: return NEUTRAL_REPORT
        state=keyboard(self.get)
        # Discard a sample if focus changed while querying the keys.
        return state if self.foreground()==self.window else NEUTRAL_REPORT

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',help='CDC1 COM port or /dev/ttyACM1')
    p.add_argument('--controller-id',type=int,default=1)
    p.add_argument('--buttons',type=lambda s:int(s,0),help='fixed bitmap for USB checks')
    p.add_argument('--seconds',type=float,help='stop after this many seconds')
    args=p.parse_args()
    try: packet(args.controller_id,0,make_report(args.buttons or 0))
    except ValueError as e: p.error(str(e))
    if args.buttons is None and os.name!='nt': p.error('keyboard capture uses Windows; use --buttons on Linux')
    try: import serial
    except ImportError: p.error('install dependency: python -m pip install pyserial')
    try: capture=FocusedKeyboard() if args.buttons is None else None
    except OSError as e: p.error(str(e))
    connection=None
    sender=None
    started=time.monotonic()
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
                    sender=Sender(connection,args.controller_id)
                    print(f'{name}: connected, waiting for READY')
                was_ready=sender.ready
                sender.poll()
                if sender.ready and not was_ready: print('READY')
                report=make_report(args.buttons) if args.buttons is not None else capture.read()
                sender.send(report)
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
