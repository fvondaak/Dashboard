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
