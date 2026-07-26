import serial
import numpy as np


class PortReader():
    def __init__(
            self,
            baudrate: int,
            port: str,
            timeout: float,
        ):
        if not port:
            raise ValueError("port must be provided")
        if baudrate <= 0:
            raise ValueError("baudrate must be positive")
        if timeout < 0:
            raise ValueError("timeout cannot be negative")

        self.baudrate = baudrate
        self.port = port
        self.timeout = timeout
        self.serial_instance = serial.Serial(
            baudrate=self.baudrate,
            port=None,
            timeout=self.timeout
            )
        
    def set_port(self, port: str) -> None:
        self.port = port
        self.serial_instance.port = self.port

    def open(self) -> None:
        '''opens serial port if not open already'''
        if self.serial_instance.is_open is True:
            return
        self.serial_instance.port = self.port
        self.serial_instance.open()

    def close(self) -> None:
        '''closes serial port if not closed already'''
        if not self.serial_instance.is_open:
            return
        self.serial_instance.close()

    def read(self, size: int) -> bytes:
        '''returns bytes from the OS buffer of size "size" '''
        return self.serial_instance.read(size=size)
    
    def is_open(self) -> bool:
        '''returns True if port is open'''
        return self.serial_instance.is_open

class PacketParser:
    def __init__(self, start_sequence: bytearray, sample_size: int, packet_length: int, max_samples: int):
        self._buffer = bytearray()
        self._start = start_sequence
        self._sample_size = sample_size
        self._packet_length = packet_length
        self._max_samples = max_samples

    def feed(self, chunk: bytes):
        '''function that appends chunck to bytearray'''
        self._buffer += bytearray(chunk)

    def search_for_start(self, start: bytearray) -> int:
        ''' returns index of the start sequence inside the bytearray'''
        return self._buffer.find(start)

    def verify_byte_array(self, packet: bytearray, max_samples):
        ''' function that verifies that start sequence is present 
            and sample number is inseide value range'''#
        if len(packet) != self._packet_length:
            return False
        if 0 < int.from_bytes(
            packet[2:4],
            byteorder="little",
            signed=False
            ) < max_samples and packet[0:2] == self._start:
            return True
        else:
            return False
        
    def extract_packet(self, start_index: int, packet_length: int, max_samples: int):
        if self.verify_byte_array(self._buffer[start_index:start_index+packet_length], max_samples=self._max_samples) is True:
            packet = bytes(self._buffer[:self._packet_length])
            del self._buffer[:self._packet_length]
            return packet
    def extract_one_packet(self):
        start_index = self.search_for_start(self._buffer)
        if start_index == -1:  # Means start sequence could not be found
            raise RuntimeError("Start sequence could not be found in buffer")
        packet = self.extract_packet(
            start_index=start_index,
            packet_length=self._packet_length,
            max_samples=self._max_samples
            )
        return packet