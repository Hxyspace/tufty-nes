import importlib.util
import struct
import sys
import unittest
from unittest.mock import Mock,patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from serial_gamepad import FocusedKeyboard,Sender,packet
from check_uf2 import inspect

class Wire:
    def __init__(self): self.rx=b''; self.tx=[]
    def write(self,data): self.tx.append(data); return len(data)
    def read(self,n): data,self.rx=self.rx[:n],self.rx[n:]; return data

class Tests(unittest.TestCase):
    def test_foreground_keyboard(self):
        for visible in (True,False):
            with self.subTest(visible_console=visible):
                api=Mock()
                window=0x123456789
                api.kernel32.GetConsoleWindow.return_value=window if visible else 42
                api.user32.IsWindowVisible.return_value=visible
                api.user32.GetForegroundWindow.return_value=window
                pressed={ord('Z'),0x1b}
                api.user32.GetAsyncKeyState.side_effect=lambda key: 0x8000 if key in pressed else 0
                with patch('ctypes.windll',api,create=True): capture=FocusedKeyboard()
                self.assertEqual(capture.window,window)
                self.assertEqual(capture.read(2),(1,True))
                w=Wire(); s=Sender(w)
                w.rx=b'#TGP:READY:'+s.nonce+b'\n'; s.poll()
                s.send(capture.read(2)[0])
                self.assertEqual(w.tx[-1],packet(1,0,1))

                # Losing focus releases a held NES button and ignores Esc.
                api.user32.GetForegroundWindow.return_value=99
                api.user32.GetAsyncKeyState.reset_mock()
                self.assertEqual(capture.read(2),(0,False))
                api.user32.GetAsyncKeyState.assert_not_called()
                s.send(capture.read(2)[0])
                self.assertEqual(w.tx[-1],packet(1,1,0))

                api.user32.GetForegroundWindow.return_value=window
                pressed={ord('B')}
                self.assertEqual(capture.read(1),(16,False))
                pressed={ord('Z')}
                self.assertEqual(capture.read(2),(1,False))
                # A focus change during a sample must also discard its keys.
                api.user32.GetForegroundWindow.side_effect=[window,99]
                self.assertEqual(capture.read(2),(0,False))

    def test_lost_handshake(self):
        w=Wire(); now=[0.0]; s=Sender(w,clock=lambda:now[0])
        old=s.nonce
        now[0]=1.21; s.poll()
        self.assertFalse(s.ready)
        self.assertNotEqual(s.nonce,old)
        self.assertEqual(len(w.tx),2)
        w.rx=b'#TGP:READY:'+s.nonce+b'\n'; s.poll()
        self.assertTrue(s.ready)

    def test_sender(self):
        w=Wire(); now=[0.0]; s=Sender(w,clock=lambda:now[0])
        self.assertTrue(w.tx[-1].startswith(b'#TGP:HELLO:0001:'))
        s.send(255); self.assertEqual(len(w.tx),1)
        w.rx=b'#TGP:READY:0000000000000000\n'; s.poll(); self.assertFalse(s.ready)
        w.rx=b'#TGP:READY:'+s.nonce+b'\n'; s.poll(); self.assertTrue(s.ready)
        s.send(255); self.assertEqual(w.tx[-1],packet(1,0,255))
        s.send(255); self.assertEqual(len(w.tx),2)
        now[0]=0.12; s.send(255); self.assertEqual(w.tx[-1],packet(1,1,255))
        old=s.nonce; now[0]=1.3; s.poll(); self.assertFalse(s.ready); self.assertNotEqual(old,s.nonce)
        w.rx=b'#TGP:READY:'+old+b'\n'; s.poll(); self.assertFalse(s.ready)
        w.rx=b'x'*200+b'#TGP:READY:'+s.nonce+b'\n'; s.poll(); s.poll(); self.assertFalse(s.ready)
        w.rx=b'#TGP:READY:'+s.nonce+b'\n'; s.poll(); s.menu()
        self.assertEqual(w.tx[-1],b'#TGP:MENU:'+s.nonce+b'\n')
        s.close(); self.assertTrue(w.tx[-1].startswith(b'#TGP:END:'))

    def test_packet(self):
        self.assertEqual(packet(1,0,31,1),b'#TGP:544750010100001f\n')
        self.assertEqual(packet(65535,256,255),b'#TGP:54475002ffff00ff\n')
        for args in ((0,0,0),(1,0,32,1),(1,0,256,2)):
            with self.assertRaises(ValueError): packet(*args)

    def test_uf2_storage_rejected(self):
        def block(address,family,flags=0x2000,number=0,total=1):
            b=bytearray(512)
            struct.pack_into('<8I',b,0,0x0a324655,0x9e5d5157,flags,address,256,number,total,family)
            struct.pack_into('<I',b,508,0x0ab16f30)
            if flags==0xa000:
                b[32:288]=b'\xef'*256
                struct.pack_into('<I',b,288,0x9957e304)
            return b
        firmware=block(0x10000000,0xe48bff59)
        compat=block(0x10ffff00,0xe48bff57,0xa000,total=2)
        self.assertEqual(inspect(compat+firmware),1)
        self.assertEqual(inspect(compat+block(0x101fff00,0xe48bff59)),1)
        for address in (0x10200000,0x10300000,0x10ffff00):
            with self.assertRaises(ValueError): inspect(compat+block(address,0xe48bff59))
        invalid=(block(0x101fff00,0xe48bff57,0xa000,total=2)+firmware,
                 block(0x10ffff00,0xe48bff57,0xa000)+firmware,
                 block(0x10ffff00,0xe48bff57,0xa000,number=1,total=2)+firmware,
                 firmware+compat,compat+compat+firmware,
                 compat+block(0x10000000,0xe48bff59,total=2),
                 compat+block(0x10000000,0xe48bff59,number=1),
                 compat+firmware+firmware)
        for data in invalid:
            with self.assertRaises(ValueError): inspect(data)
        for offset in (32,288):
            data=bytearray(compat+firmware); data[offset]^=1
            with self.assertRaises(ValueError): inspect(data)

if __name__=='__main__': unittest.main()
