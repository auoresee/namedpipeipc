import time
import os
import threading
import json
from .named_pipe import NamedPipe

IPC_MESSAGE_HEADER_SIZE = 152

class IPCMessageDirection:
    ONE_WAY = 0
    REQUEST = 1
    RESPONSE = 2

class IPCMessageDataType:
    BINARY = 0
    STRING = 1
    JSON = 2

class IPCMessage:
    def __init__(self, data: bytes | str, data_type: int, direction: int, message_type: str = "", message_id: str = None):
        if type(data) == str:
            data = data.encode() + b'\0'
        self.data = data
        self.data_type = data_type
        self.direction = direction
        self.message_type = message_type
        self.message_id = message_id

    def __len__(self):
        return self.get_header_size() + len(self.data)
    
    def get_header_size(self):
        return IPC_MESSAGE_HEADER_SIZE
    
    def encode_string_as_bytes(self, string: str, size: int):
        buf = bytes()
        encoded = string.encode()
        if len(encoded) > size:
            raise Exception("IPCMessage: String is too long; max: " + str(size) + ", actual: " + str(len(encoded)))
        buf += encoded
        buf += b'\0' * (size - len(string))
        return buf
    
    def get_header_as_bytes(self):
        if self.message_id == None:
            raise Exception("IPCMessage: Message ID is not set.")
        buf = bytes()
        buf += self.get_header_size().to_bytes(8, 'little')
        buf += len(self.data).to_bytes(8, 'little')
        buf += self.encode_string_as_bytes(self.message_id, 64)
        buf += self.data_type.to_bytes(4, 'little')
        buf += self.direction.to_bytes(4, 'little')
        buf += self.encode_string_as_bytes(self.message_type, 64)
        return buf
    
    def to_bytes(self):
        return self.get_header_as_bytes() + self.data
    
    @staticmethod
    def from_bytes(buf: bytes):
        ret = IPCMessage(None, None, None)
        if len(buf) < ret.get_header_size():
            raise Exception("IPCMessage: Buffer is too short; min: " + str(ret.get_header_size()) + ", actual: " + str(len(buf)))
        header_size = int.from_bytes(buf[0:8], 'little')
        if header_size != ret.get_header_size():
            raise Exception("IPCMessage: Header size mismatch; expected: " + str(ret.get_header_size()) + ", actual: " + str(header_size))
        data_size = int.from_bytes(buf[8:16], 'little')
        if len(buf) < ret.get_header_size() + data_size:
            raise Exception("IPCMessage: Message size mismatch; header says: " + str(ret.get_header_size() + data_size) + ", actual: " + str(len(buf)))
        ret.message_id = buf[16:80].decode().rstrip('\0')
        ret.data_type = int.from_bytes(buf[80:84], 'little')
        ret.direction = int.from_bytes(buf[84:88], 'little')
        ret.message_type = buf[88:152].decode().rstrip('\0')
        ret.data = buf[ret.get_header_size():]
        return ret

    def get_raw_data(self):
        return self.data
    
    def get_data_as_string(self, encoding: str = "utf-8", ignore_type_mismatch: bool = False):
        if self.data_type != IPCMessageDataType.STRING and not ignore_type_mismatch:
            raise Exception("IPCMessage: Data type mismatch; expected: " + str(IPCMessageDataType.STRING) + ", actual: " + str(self.data_type))
        return self.data.decode(encoding)
    
    def get_data_as_dict(self, ignore_type_mismatch: bool = False):
        if self.data_type != IPCMessageDataType.JSON and not ignore_type_mismatch:
            raise Exception("IPCMessage: Data type mismatch; expected: " + str(IPCMessageDataType.JSON) + ", actual: " + str(self.data_type))
        return json.loads(self.get_data_as_string())
    

class IPCManager:
    def __init__(self, pipe: NamedPipe, is_server: bool):
        self.pipe = pipe
        self.is_server = is_server
        self.last_message_id = 0
        self.message_id_lock = threading.Lock()
        self.message_type_handler_map = {}
        self.message_id_response_handler_map = {}

    def connect(self):
        if self.is_server:
            raise Exception("IPCManager: connect: This function is not supported in server mode.")
        else:
            self.pipe.connect()

    def wait_for_connection(self):
        if self.is_server:
            self.pipe.wait_for_connection()
        else:
            raise Exception("IPCManager: wait_for_connection: This function is not supported in client mode.")
    
    def get_message_id(self) -> str:
        with self.message_id_lock:
            self.last_message_id += 1
            return str(self.last_message_id)
    
    def send(self, message: IPCMessage, response_handler: callable = None) -> bool:
        # if message id is not set, set it
        if message.message_id == None:
            message.message_id = self.get_message_id()
        # if message is a request, register a response handler
        if message.direction == IPCMessageDirection.REQUEST:
            if response_handler == None:
                # if no response handler is provided, create a dummy handler
                def dummy_handler(message: IPCMessage):
                    pass
                response_handler = dummy_handler
            self.message_id_response_handler_map[message.message_id] = response_handler
        return self.pipe.write(message.to_bytes())
    

    def read(self, size: int = None) -> bytes:
        ret = bytes()
        while True:
            data = self.pipe.read(size)
            if data == None:
                if len(ret) > 0:
                    raise Exception("IPCManager: Unexpected end of stream.")
                return None
            ret += data
            # check length
            if size != None:
                if len(ret) >= size:
                    return ret


    def receive(self) -> IPCMessage:
        buf1 = self.read(IPC_MESSAGE_HEADER_SIZE)
        if buf1 == None:
            return None
        buf2 = self.read(int.from_bytes(buf1[8:16], 'little'))
        if buf2 == None:
            raise Exception("IPCManager: Unexpected end of stream.")
        buf = buf1 + buf2

        message = IPCMessage.from_bytes(buf)
        return message
    
    def wait_receive(self) -> None:
        while True:
            try:
                message = self.receive()
            except Exception as e:
                print("IPCManager: " + str(e))
                return
            if message != None:
                # if message is a response, look for a response handler
                if message.direction == IPCMessageDirection.RESPONSE:
                    handler = self.message_id_response_handler_map.get(message.message_id)
                    if handler != None:
                        handler(message)
                    else:
                        print("IPCManager: No response handler for message ID: " + message.message_id)
                else:
                    # if message is not a response, look for a handler
                    handler = self.message_type_handler_map.get(message.message_type)
                    if handler != None:
                        handler(message)
                    else:
                        print("IPCManager: No handler for message type: " + message.message_type)
            else:
                time.sleep(0.01)

    def wait_response(self, message_id: str, timeout: float = 5.0) -> IPCMessage:
        start_time = time.time()
        while True:
            message = self.receive()

            if message != None:
                if message.direction == IPCMessageDirection.RESPONSE and message.message_id == message_id:
                    return message
            if time.time() - start_time > timeout:
                raise Exception("IPCManager: wait_response: Timeout.")
            time.sleep(0.01)

    def request(self, data: bytes | str, data_type: int, message_type: str, timeout: float = 5.0) -> IPCMessage:
        # create message
        message = IPCMessage(data, data_type, IPCMessageDirection.REQUEST, message_type)
        # send message
        self.send(message)
        # wait for response
        response = self.wait_response(message.message_id, timeout)
        return response

    def register_message_type_handler(self, message_type: str, handler: callable):
        self.message_type_handler_map[message_type] = handler

    def unregister_message_type_handler(self, message_type: str):
        self.message_type_handler_map.pop(message_type, None)

    def unregister_all_message_type_handlers(self):
        self.message_type_handler_map.clear()