import os
import time
import subprocess
from named_pipe_ipc_win import *
from ipc_manager import *

def test1():
    message = IPCMessage("Hello", IPCMessageDataType.STRING, IPCMessageDirection.ONE_WAY, "test")
    pipe = NamedPipe("test_pipe")
    manager = IPCManager(pipe, False)
    manager.connect()
    #manager.send(message)
    response = manager.request("Hello", IPCMessageDataType.STRING, "test")
    pipe.disconnect()
    string = response.data.decode()
    print(string)

# server test
def test():
    pipe_name = "test_pipe"
    pipe = NamedPipe(pipe_name)
    manager = IPCManager(pipe, True)
    # start process
    print("Starting process...")
    process = subprocess.Popen(["./cpptest/Release/NamedPipeIPCTest.exe", pipe_name])
    # check if the process is running
    if process.poll() != None:
        raise Exception("Process exited with code " + str(process.returncode))
    print("Waiting for connection...")
    manager.wait_for_connection()
    print("Connected.")
    req = {
        "method": "get",
        "params": {
            "id": 1,
            "name": "test",
            "requested_size": 4*1024*1024
        }
    }
    jsonreq = json.dumps(req)
    response = manager.request(jsonreq, IPCMessageDataType.JSON, "test1")
    pipe.disconnect()
    print("data size: " + str(len(response.data)))
    string = response.data.decode()
    print(string)

if __name__ == "__main__":
    test()