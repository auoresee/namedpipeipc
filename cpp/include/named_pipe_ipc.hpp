#pragma once
#pragma comment(lib, "bcrypt.lib")

#include <stdio.h>
#include <windows.h>
#include <vector>
#include "nlohmann/json.hpp"
#include "named_pipe_ipc.hpp"
#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <thread>

// for uuid
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

using namespace std;

#define IPC_MESSAGE_HEADER_SIZE 152

#define IPC_MESSAGE_DATA_TYPE_BINARY 0
#define IPC_MESSAGE_DATA_TYPE_STRING 1
#define IPC_MESSAGE_DATA_TYPE_JSON 2

#define IPC_MESSAGE_DIRECTION_ONEWAY 0
#define IPC_MESSAGE_DIRECTION_REQUEST 1
#define IPC_MESSAGE_DIRECTION_RESPONSE 2

#define MMAP_READ_SLEEP_MSEC 10

/*struct IPCMessageStruct
{
	unsigned long long header_size; // 8 + 8 + 64 + 4 + 4 + 64 = 152
	unsigned long long data_size;
	char message_id[64];    // request and response should have the same id
	int data_type;          // binary: 0, string: 1, json: 2
	int direction;          // one way: 0, request: 1, response: 2
	char label[64]; // label to determine the message handler
	const char* data;
};*/

vector<char> uint64ToBytes(unsigned long long value, bool little_endian = true)
{
	vector<char> bytes(8);
	if (little_endian)
	{
		for (int i = 0; i < 8; i++)
		{
			bytes[i] = (value >> (i * 8)) & 0xFF;
		}
	}
	else
	{
		for (int i = 0; i < 8; i++)
		{
			bytes[i] = (value >> ((7 - i) * 8)) & 0xFF;
		}
	}
	return bytes;
}

unsigned long long bytesToUint64(const unsigned char* bytes, bool little_endian = true)
{
	unsigned long long value = 0;
	if (little_endian)
	{
		for (int i = 0; i < 8; i++)
		{
			value |= ((unsigned long long)bytes[i]) << (i * 8);
		}
	}
	else
	{
		for (int i = 0; i < 8; i++)
		{
			value |= ((unsigned long long)bytes[i]) << ((7 - i) * 8);
		}
	}
	return value;
}

vector<char> int32ToBytes(int value, bool little_endian = true)
{
	vector<char> bytes(4);
	if (little_endian)
	{
		for (int i = 0; i < 4; i++)
		{
			bytes[i] = (value >> (i * 8)) & 0xFF;
		}
	}
	else
	{
		for (int i = 0; i < 4; i++)
		{
			bytes[i] = (value >> ((3 - i) * 8)) & 0xFF;
		}
	}
	return bytes;
}

int bytesToInt32(const unsigned char* bytes, bool little_endian = true)
{
	int value = 0;
	if (little_endian)
	{
		for (int i = 0; i < 4; i++)
		{
			value |= ((int)bytes[i]) << (i * 8);
		}
	}
	else
	{
		for (int i = 0; i < 4; i++)
		{
			value |= ((int)bytes[i]) << ((3 - i) * 8);
		}
	}
	return value;
}

enum IPCExceptionType
{
	UNKNOWN_ERROR = 0,
	UNSUPPORTED_FUNCTION,
	BROKEN_PIPE,
	PARAMETER_TOO_LONG,
	FAILED_TO_CREATE,
	FAILED_TO_CONNECT,
};

class IPCException : public std::runtime_error
{
public:     //元々std::runtime_errorにある文字列↓   ↓追加の情報

	IPCException(const char* _Message, int res)
		: _Errinfo(res), runtime_error(_Message)
		//↑こうすることでwhat()で人間向けメッセージが読める
	{}

	//↓追加のエラー情報を返すGetter
	int returncode()
	{
		return _Errinfo;
	}
private:
	int _Errinfo;
};

void showWinError(int error_code)
{
	// Retrieve the system error message for the last-error code

	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	//DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)& lpMsgBuf,
		0, NULL);

	puts((LPTSTR)lpMsgBuf);
}

class NamedPipe final
{
public:
	HANDLE _pipe_handle;
	bool is_server;
	int buffer_size;
	const char* pipe_name;

	explicit NamedPipe(
		const char* pipe_name,
		bool is_server = true,
		const int& buffer_size = 65536
	)
	{
		this->is_server = is_server;
		this->buffer_size = buffer_size;
		this->pipe_name = pipe_name;
	}

	~NamedPipe()
	{
		CloseHandle(_pipe_handle);
	}

	// prohibit copy
	NamedPipe(const NamedPipe&) = delete;
	NamedPipe& operator = (const NamedPipe&) = delete;

	void open() {
		string pipe_path = string("\\\\.\\pipe\\") + pipe_name;
		if (is_server) {
			_pipe_handle = CreateNamedPipeA(
				pipe_path.c_str(),
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				buffer_size,
				buffer_size,
				0,
				NULL
			);
			if (_pipe_handle == INVALID_HANDLE_VALUE) throw IPCException("pipe creation failed", IPCExceptionType::FAILED_TO_CREATE);
		}
		else {
			_pipe_handle = CreateFileA(
				pipe_path.c_str(),
				GENERIC_READ | GENERIC_WRITE,
				0,
				NULL,
				OPEN_EXISTING,
				0,
				NULL
			);
			if (_pipe_handle == INVALID_HANDLE_VALUE) throw IPCException("pipe connection failed", IPCExceptionType::FAILED_TO_CONNECT);
		}
	}

	// server side
	// wait for client to connect
	void connect()
	{
		if (!ConnectNamedPipe(_pipe_handle, NULL))
		{
			if (GetLastError() != ERROR_PIPE_CONNECTED) throw "pipe connection failed";
		}
	}

	// server side
	// disconnect client
	void disconnect()
	{
		DisconnectNamedPipe(_pipe_handle);
	}

	int read(char* buf, int max_size)
	{
		DWORD bytes_read;
		if (!ReadFile(_pipe_handle, buf, max_size, &bytes_read, NULL)) {
			int err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				throw IPCException("Error: IO pending is not supported.", IPCExceptionType::UNSUPPORTED_FUNCTION);
			}
			
			if (err == ERROR_BROKEN_PIPE) {
				throw IPCException("Error: Broken pipe", IPCExceptionType::BROKEN_PIPE);
			}
			showWinError(err);
			throw IPCException("Error: Failed to read from pipe", err);
		}
		return bytes_read;
	}

	int write(const char* buf, int size)
	{
		DWORD bytes_written;
		if (!WriteFile(_pipe_handle, buf, size, &bytes_written, NULL)) throw "pipe write failed";
		return bytes_written;
	}
};

class IPCMessage
{
public:
	explicit IPCMessage(int data_type, int direction, string message_type, unsigned long long data_size, const char* data, const char* message_id = NULL)
	{
		this->data_size = data_size;
		this->data = new char[data_size];
		memcpy(this->data, data, data_size);
		this->data_type = data_type;
		this->direction = direction;
		this->message_type = message_type;
		if (message_id != NULL) {
			this->message_id = new char[64];
			memcpy(this->message_id, message_id, 64);
		}
	}

	// prohibit copy (use clone() or std::move())
	IPCMessage(const IPCMessage&) = delete;
	IPCMessage& operator = (const IPCMessage&) = delete;
	
	// move constructor
	IPCMessage(IPCMessage&& v) noexcept :
		data_size(v.data_size),
		data(v.data),
		data_type(v.data_type),
		direction(v.direction),
		message_type(v.message_type),
		message_id(v.message_id) {
		v.message_id = nullptr;
		v.data = nullptr;
	}

	// move operator=
	IPCMessage& operator=(IPCMessage&& v) noexcept {
		delete[] this->data;
		delete[] this->message_id;
		this->data_size = data_size;
		this->data = v.data;
		this->data_type = data_type;
		this->direction = direction;
		this->message_type = message_type;
		this->message_id = message_id;
		v.message_id = nullptr;
		v.data = nullptr;
		return *this;
	}

	~IPCMessage() {
		delete[] data;
		delete[] message_id;
	}

	IPCMessage clone() {
		return IPCMessage(data_type, direction, message_type, data_size, data, message_id);
	}

	unsigned long long getSize() const { return data_size; }
	const char* getRawData() const { return data; }
	std::string getDataAsString(bool ignore_data_type_mismatch = false) const {
		// check data type
		if (!ignore_data_type_mismatch && data_type != IPC_MESSAGE_DATA_TYPE_STRING)
		{
			throw std::runtime_error("data type is not string");
		}
		return std::string((const char*)data, data_size);
	}

	nlohmann::json getDataAsJson(bool ignore_data_type_mismatch = false) const {
		// check data type
		if (!ignore_data_type_mismatch && data_type != IPC_MESSAGE_DATA_TYPE_JSON)
		{
			throw std::runtime_error("data type is not json");
		}
		return nlohmann::json::parse(getDataAsString(true));
	}

	// generates response to this message
	IPCMessage generate_response(const char* data, int data_size, int data_type) const {
		return IPCMessage(data_type, IPC_MESSAGE_DIRECTION_RESPONSE, message_type, data_size, data, message_id);
	}

	// serialize to binary
	std::vector<char> serialize() const
	{
		char* buf = new char[IPC_MESSAGE_HEADER_SIZE + data_size];

		memcpy(buf, uint64ToBytes(IPC_MESSAGE_HEADER_SIZE).data(), 8);
		memcpy(buf + 8, &data_size, 8);
		memcpy(buf + 16, message_id, 64);
		memcpy(buf + 80, int32ToBytes(data_type).data(), 4);
		memcpy(buf + 84, int32ToBytes(direction).data(), 4);
		if (message_type.length() > 63) {
			throw IPCException("Message type is too long; maximum is 63 bytes", IPCExceptionType::PARAMETER_TOO_LONG);
		}
		strncpy(buf + 88, message_type.c_str(), 64);
		// data
		memcpy(buf + IPC_MESSAGE_HEADER_SIZE, data, (size_t) data_size);

		auto ret = vector<char>(buf, buf + IPC_MESSAGE_HEADER_SIZE + data_size);
		delete[] buf;
		return ret;
	}

	// deserialize from binary
	static IPCMessage deserialize(const std::vector<char> & buf)
	{
		const unsigned char* pbuf = (const unsigned char*) buf.data();
		// header
		unsigned long long header_size = bytesToUint64(pbuf, true);
		unsigned long long data_size = bytesToUint64(pbuf + 8, true);
		const char* message_id = (const char*)pbuf + 16;
		int data_type = bytesToInt32(pbuf + 80, true);
		int direction = bytesToInt32(pbuf + 84, true);
		string message_type = string((const char*)pbuf + 88);
		// data
		const char* data = (const char*)pbuf + IPC_MESSAGE_HEADER_SIZE;
		return IPCMessage(data_type, direction, message_type, data_size, data, message_id);
	}

	char* message_id;      // request and response should have the same id
	unsigned long long data_size;
	int data_type;          // binary: 0, string: 1, json: 2
	int direction;       // one way: 0, request: 1, response: 2
	string message_type; // label to determine the message handler
	char* data;
};

class IPCManager
{
public:
	IPCManager(NamedPipe* pipe, bool is_server = true)
	{
		this->pipe = pipe;
		this->is_server = is_server;
	}

	void wait_for_connection(void) {
		pipe->connect();
	}

	// prohibit copy
	IPCManager(const IPCManager&) = delete;
	IPCManager& operator = (const IPCManager&) = delete;

	// send message
	bool send(IPCMessage& message, function<void(const IPCMessage&)> response_handler = NULL);

	int write(const char* buf, int size)
	{
		return pipe->write(buf, size);
	}

	vector<char> read_and_alloc(int max_size, bool retry = false);

	// receive message, synchronous
	IPCMessage receive();

	void IPCManager::wait_receive();

	void register_handler(string message_type, function<void(const IPCMessage&)> handler)
	{
		message_type_handler_map[message_type] = handler;
	}

	void register_request_handler(string message_type, function<IPCMessage(const IPCMessage&)> handler)
	{
		message_type_request_handler_map[message_type] = handler;
	}
	
	const char* generate_id() {
		return generate_uuid();
	}

	void sleep_msec(int msec)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(msec));
	}

	void generate_and_set_message_id(IPCMessage& message) {
		const int UUID_LENGTH = 36;
		message.message_id = new char[64]{ 0 };
		memcpy(message.message_id, generate_id(), UUID_LENGTH);
	}

private:
	int mode = 0;
	bool is_server;
	NamedPipe* pipe;
	map<string, function<IPCMessage(const IPCMessage&)>> message_type_request_handler_map;
	map<string, function<void(const IPCMessage&)>> message_type_handler_map;
	map<string, function<void(const IPCMessage&)>> message_id_response_handler_map;

	const char* generate_uuid() {
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		return boost::lexical_cast<std::string>(uuid).c_str();
	}
};

// send message
bool IPCManager::send(IPCMessage& message, function<void(const IPCMessage&)> response_handler)
{
	// if message id is not specified, generate a new one
	if (message.message_id == NULL)
	{
		generate_and_set_message_id(message);
	}
	// if message is a request, register the response handler
	if (message.direction == IPC_MESSAGE_DIRECTION_REQUEST)
	{
		message_id_response_handler_map[string(message.message_id)] = response_handler;
	}

	std::vector<char> buf = message.serialize();
	int bytes_written = write(buf.data(), buf.size());
	return bytes_written == buf.size();
}

vector<char> IPCManager::read_and_alloc(int max_size, bool retry)
{
	char* buf = new char[max_size];
	int pos = 0;
	while (true) {
		int bytes_read = pipe->read(buf, max_size);
		pos += bytes_read;
		if (!retry) break;
		if (bytes_read == max_size) {
			break;
		}
	}
	auto ret = vector<char>(buf, buf + pos);
	delete[] buf;
	return ret;
}

// receive message, synchronous
IPCMessage IPCManager::receive()
{
	vector<char> buf = read_and_alloc(16);
	if (buf.size() != 16) throw "pipe read failed";
	unsigned long long header_size = bytesToUint64((unsigned char*)buf.data(), true);
	vector<char> buf2 = read_and_alloc(header_size - 16, true);
	if (buf2.size() < header_size - 16) throw "pipe read failed";
	unsigned long long data_size = bytesToUint64((unsigned char*)buf.data() + 8, true);
	buf.insert(buf.end(), buf2.begin(), buf2.end());
	vector<char> buf3 = read_and_alloc(data_size, true);
	if (buf3.size() < data_size) throw "pipe read failed";
	buf.insert(buf.end(), buf3.begin(), buf3.end());
	return IPCMessage::deserialize(buf);
}

// wait for message (server side)
void IPCManager::wait_receive()
{
	try {
		while (true)
		{
			IPCMessage message = receive();

			// if message is a response, call the response handler
			if (message.direction == IPC_MESSAGE_DIRECTION_RESPONSE)
			{
				auto f = message_id_response_handler_map.find(string(message.message_id));
				if (f != message_id_response_handler_map.end())
				{
					auto handler = f->second;
					if (handler != NULL)
					{
						handler(message);
					}
				}
			}
			else    // if message is not a response, call the message type handler
			{
				auto f = message_type_request_handler_map.find(message.message_type);
				if (f != message_type_request_handler_map.end())
				{
					auto handler = f->second;
					if (handler != NULL)
					{
						auto response = handler(message);
						send(response);
					}
				}
				auto f2 = message_type_handler_map.find(message.message_type);
				if (f2 != message_type_handler_map.end())
				{
					auto handler = f2->second;
					if (handler != NULL)
					{
						handler(message);
					}
				}
			}
		}
	}
	catch (IPCException & e) {
		// pipe is closed
		if (e.returncode() == IPCExceptionType::BROKEN_PIPE) {
			return;
		}
		else {
			cout << e.what();
			throw e;
		}
	}
}
