#pragma once

#include <iostream>

#include "named_pipe_ipc.hpp"

IPCMessage test_handler(const IPCMessage& message)
{
    cout << "test_handler" << endl;
    cout << message.getDataAsString() << endl;
	string m = "OK";
	return message.generate_response(m.c_str(), m.length() + 1, IPC_MESSAGE_DATA_TYPE_STRING);
}

int main(void)
{
    NamedPipe pipe("test_pipe", true);
	IPCManager manager(&pipe, true);
	ipe.open();
	manager.wait_for_connection();
	manager.register_request_handler("test", test_handler);
    manager.wait_receive();
    return 0;
}