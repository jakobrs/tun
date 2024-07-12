/-- Corresponds to read syscall -/
@[extern "sys_read"]
opaque sys_read (fd : @& Int) (buf : @& ByteArray) : IO ByteArray
/-- Corresponds to write syscall -/
@[extern "sys_write"]
opaque sys_write (fd : @& Int) (data : @& ByteArray) : IO Unit

opaque TunPointed : NonemptyType
/-- A tun device. Closed automatically when refcount reaches zero -/
def Tun : Type := TunPointed.type
instance : Nonempty Tun := TunPointed.property

/-- Open a new tun device -/
@[extern "tun_mk"]
opaque Tun.mk (name : @& Option String := none) : IO Tun
/-- The name of the tun device -/
@[extern "tun_name"]
opaque Tun.name (dev : @& Tun) : String
/-- The file descriptor corresponding to this tun device -/
@[extern "tun_fd"]
opaque Tun.fd (dev : @& Tun) : Int

opaque UdpPointed : NonemptyType
/-- A UDP socket. Closed automatically when refcount reaches zero -/
def Udp : Type := UdpPointed.type
instance : Nonempty Udp := UdpPointed.property

/-- Create and bind a UDP socket on 0.0.0.0:3456 -/
@[extern "udp_mk"]
opaque Udp.mk (port : @& UInt16) : IO Udp
/-- The file descriptor corresponding to this UDP socket -/
@[extern "udp_fd"]
opaque Udp.fd (sock : @& Udp) : Int
@[extern "udp_connect"]
opaque Udp.connect (sock : @& Udp) (addr : @& String) (port : @& UInt16) : IO Unit

/-- Creates a byte array from a string (utf-8 encoded). O(n) due to strcpy -/
@[extern "string_to_byte_array"]
opaque String.toByteArray (str : @& String) : ByteArray

structure UdpSocket where
  sock : Udp

def UdpSocket.new (port : UInt16) := UdpSocket.mk <$> Udp.mk port
def UdpSocket.fd (sock : UdpSocket) := sock.sock.fd
def UdpSocket.read (sock : UdpSocket) := sys_read sock.fd
def UdpSocket.write (sock : UdpSocket) := sys_write sock.fd
def UdpSocket.connect (sock : UdpSocket) := sock.sock.connect

structure TunDev where
  tun : Tun

def TunDev.new (name : Option String := none) := TunDev.mk <$> Tun.mk name
def TunDev.fd (dev : TunDev) := dev.tun.fd
def TunDev.read (dev : TunDev) := sys_read dev.fd
def TunDev.write (dev : TunDev) := sys_write dev.fd

partial def forever [Monad m] (action : m Unit) : m Unit := do
  action
  forever action

def main : IO Unit := do
  let sock <- UdpSocket.new 5432
  sock.connect "127.0.0.1" 2345
  let tun <- TunDev.new

  let sender <- IO.asTask $ forever do
    let buf := ByteArray.mkEmpty 1500
    let buf <- tun.read buf
    println! "sending s{buf.size} bytes"
    sock.write buf

  let receiver <- IO.asTask $ forever do
    let buf := ByteArray.mkEmpty 1500
    let buf <- sock.read buf
    println! "received s{buf.size} bytes"
    tun.write buf

  let _ := sender.get
  let _ := receiver.get
