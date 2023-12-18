# -*- coding: utf-8 -*-

"""Named pipe client object."""

import win32pipe
import win32file
import pywintypes
import winerror


class NamedPipe():

    def __init__(self, name, is_server=False):
        """
                Sets the name of the pipe to connect.

                :param name: Name of the pipe to connect.
                :type name: str
        """
        self.name = name
        self.handle = None
        self.is_server = is_server

    def connect(self):
        if self.is_server:
            self.wait_for_connection()
        else:
            """Connects to the pipe specified in the constructor."""
            try:
                self.handle = win32file.CreateFile("\\\\.\\pipe\\%s" % (
                    self.name), win32file.GENERIC_READ | win32file.GENERIC_WRITE, 0, None, win32file.OPEN_EXISTING, 0, None)
            except pywintypes.error as e:
                if e.winerror == winerror.ERROR_FILE_NOT_FOUND:
                    raise PipeServerNotFoundError(e.strerror)
                else:
                    raise e
            # end except
            #res = win32pipe.SetNamedPipeHandleState(
            #    self.handle, win32pipe.PIPE_READMODE_MESSAGE, None, None)
            #if res == 0:
            #    raise PipeError("SetNamedPipeHandleState failed.")

    def wait_for_connection(self):
        """Waits for a client to connect to the pipe."""
        self.handle = win32pipe.CreateNamedPipe("\\\\.\\pipe\\" + self.name,
            win32pipe.PIPE_ACCESS_DUPLEX,
            win32pipe.PIPE_TYPE_BYTE | win32pipe.PIPE_READMODE_BYTE | win32pipe.PIPE_WAIT,
            win32pipe.PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, None)
        if self.handle == win32file.INVALID_HANDLE_VALUE:
            raise PipeError("CreateNamedPipe failed.")
        win32pipe.ConnectNamedPipe(self.handle, None)

    def disconnect(self):
        """Disconnects from the pipe."""
        if self.handle:
            win32file.CloseHandle(self.handle)

    def write(self, data: bytes):
        """
            Writes string data to the connected pipe.
        """
        #size = len(data)
        #win32file.WriteFile(self.handle, size.to_bytes(8, 'little'))
        win32file.WriteFile(self.handle, data)
        
    def read(self, size: int) -> bytes:
        """
            Reads string data from the connected pipe.
        """

        #hr, size_bytes = win32file.ReadFile(self.handle, 8)
        #if hr == winerror.ERROR_IO_PENDING:
        #    return None
        #if len(size_bytes) != 8:
        #    raise Exception("NamedPipeIPC: Header size mismatch; expected: " + str(8) + ", actual: " + str(len(size_bytes)))
        #size = int.from_bytes(size_bytes, 'little')
        hr, data = win32file.ReadFile(self.handle, size)
        if hr == winerror.ERROR_IO_PENDING:
            raise Exception("NamedPipeIPC: IO pending is not supported.")
        #if len(data) != size:
        #    raise Exception("NamedPipeIPC: Message size mismatch; header says: " + str(size) + ", actual: " + str(len(data)))
        
        return data
        


class PipeServerNotFoundError(Exception):
    pass


class PipeBusyError(Exception):
    pass


class PipeError(Exception):
    pass