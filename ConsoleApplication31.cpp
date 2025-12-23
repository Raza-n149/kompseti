//сервер
﻿#include <fstream>
#include <iostream>
#include <WinSock2.h>
#include <windows.h>
#include <WS2tcpip.h>
#include <map>
#include <cstdint>
#include <thread>
#include <sstream>
#include <string>
#pragma comment(lib, "Ws2_32.lib")
#define PORT 8000
#define BUF_SIZE 8192
using namespace std;

map<string, string> STATUS_CODES = {
	{"404", "Not found"},
	{"200", "OK"}
};

#pragma comment(lib, "IPHLPAPI.lib")
#include <iphlpapi.h>
#include <vector>
#include <ws2ipdef.h>
string getLocalIP() {
	ULONG buflen = 15000;
	std::vector<BYTE> buffer(buflen);
	PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)buffer.data();
	DWORD res = GetAdaptersAddresses(
		AF_INET,
		GAA_FLAG_SKIP_ANYCAST |
		GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER,
		nullptr,
		adapters,
		&buflen);
	if (res != ERROR_SUCCESS) {
		return "Unknown";
	}
	for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
		if (adapter->OperStatus != IfOperStatusUp) continue;
		if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
		if (adapter->Flags & IP_ADAPTER_NO_MULTICAST) continue;

		for (PIP_ADAPTER_UNICAST_ADDRESS addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
			if (addr->Address.lpSockaddr->sa_family == AF_INET) {
				char ipStr[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &((sockaddr_in*)addr->Address.lpSockaddr)->sin_addr, ipStr, sizeof(ipStr));
				return std::string(ipStr);
			}
		}
	}

	return "Unknown";
}


map<string, string> MIME_TYPES = {
	{"html", "text/html; charset=utf-8"},
	{"htm",  "text/html; charset=utf-8"},
	{"txt",  "text/plain; charset=utf-8"},
	{"css",  "text/css"},
	{"js",   "application/javascript"},
	{"mjs",  "application/javascript"},
	{"png",  "image/png"},
	{"jpg",  "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"gif",  "image/gif"},
	{"bmp",  "image/bmp"},
	{"webp", "image/webp"},
	{"svg",  "image/svg+xml"},
	{"woff",  "font/woff"},
	{"woff2", "font/woff2"},
	{"ttf",   "font/ttf"},
	{"otf",   "font/otf"},
	{"eot",   "application/vnd.ms-fontobject"},
	{"ico",  "image/x-icon"},
	{"json", "application/json"},
	{"xml",  "application/xml"},
	{"zip",  "application/zip"},
	{"gz",   "application/gzip"},
	{"mp4",  "video/mp4"},
	{"webm", "video/webm"},
	{"ogg",  "video/ogg"},
	{"mp3",  "audio/mpeg"},
	{"wav",  "audio/wav"},
	{"ogg",  "audio/ogg"},
	{"m4a",  "audio/mp4"},
	{"pdf",  "application/pdf"},
	{"csv",  "text/csv; charset=utf-8"},
	{"md",   "text/markdown; charset=utf-8"}
};

bool fileExists(const std::string& filename) {
	DWORD attrs = GetFileAttributesA(filename.c_str());
	return (attrs != INVALID_FILE_ATTRIBUTES) &&
		!(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline string getRes(const string& filename) {
	size_t end = filename.find_last_of('.');
	return filename.substr(end + 1);
}

string make_http_response(string http_ver, string status, string type, string connection, string body) {
	stringstream response;
	response << "HTTP/" << http_ver << " " << status << " " << STATUS_CODES[status] << "\r\n";
	response << "Content-Type: " << type << "\r\n";
	response << "Connection: " << connection << "\r\n";
	response << "Content-Length: " << body.size() << "\r\n";
	response << "\r\n";
	response << body;

	return response.str();
}

struct HTTP_REQUEST {
	string mime;
	string path;
	string connection;
};

HTTP_REQUEST parse_HTTP(const char* buf) {
	string request(buf);
	size_t end_type = request.find(" ", 0);
	string type = request.substr(0, end_type);
	size_t end_path = request.find(" ", end_type + 1);
	string path = request.substr(end_type + 1, end_path - end_type - 1);
	size_t start_con = request.find("Connection:");
	string connection;
	if (start_con != string::npos) {
		size_t end_con = request.find("\r\n", start_con);
		connection = request.substr(start_con + 12, end_con - start_con - 12);
	}
	if (path[path.size() - 1] == '/') path += "index.html";
	path = path.substr(1);
	return { type, path, connection };
}

string getFileContent(const string& filename) {
	ifstream file(filename, ios::binary);
	if (!file.is_open()) {
		return "";
	}
	std::ostringstream oss;
	oss << file.rdbuf();
	return oss.str();
}

void static browser_handler(SOCKET client) {
	char buf[BUF_SIZE];
	while (true) {
		int n = recv(client, buf, BUF_SIZE - 1, 0);
		if (n <= 0) {
			break;
		}
		buf[n] = '\0';
		string ss(buf);
		HTTP_REQUEST req = parse_HTTP(buf);
		cout << "Received request: \n" << ss << '\n';
		string resp;
		string resl = getRes(req.path);
		if (!fileExists(req.path)) {
			if (resl == "html") {
				string content = getFileContent("404.html");
				if (content.size() > 0)
					resp = make_http_response("1.1", "404", "text/html", "close", content);
			}
			else
				resp = make_http_response("1.1", "404", "text/plain", "keep-alive", "File \"" + req.path + "\" is not founded on server");
			cout << "File: " << req.path << " not founded locally on server\n";
		}
		else {
			string content = getFileContent(req.path);
			resp = make_http_response("1.1", "200", MIME_TYPES[resl], "keep-alive", content);
		}
		send(client, resp.c_str(), resp.size(), 0);
	}
	cout << "Browser disconnected\r\n";
}

int main() {
	setlocale(LC_ALL, "Russian");
	SetConsoleCP(1251);
	SetConsoleOutputCP(1251);
	WSADATA wsaData;
	{
		int result = WSAStartup(0x202, &wsaData);
		if (result) {
			cerr << "WSAStartup error: " << result << '\n';
			return -1;
		}
	}
	SOCKET s, client_socket;
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		cerr << "Socket initialization error: " << WSAGetLastError() << '\n';
		WSACleanup();
		return -1;
	}
	sockaddr_in serv_addr;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_family = AF_INET;
	{
		int result = bind(s, (sockaddr*)&serv_addr, sizeof(serv_addr));
		if (result) {
			cerr << "bind error: " << result << '\n';
			closesocket(s);
			WSACleanup();
			return -1;
		}
	}
	cout << "Web-Server | by Mas-Eg | https://github.com/Mas-Eg \n\nServer is working and awaiting for requests...\n\nAvailable on:\n localhost:8000\n " << getLocalIP() << ":8000\n\n";
	listen(s, SOMAXCONN);
	while (true) {
		sockaddr_in client_addr;
		int client_addr_len = sizeof(client_addr);
		client_socket = accept(s, (sockaddr*)&client_addr, &client_addr_len);
		thread(browser_handler, client_socket).detach();
	}
	closesocket(s);
	WSACleanup();
	return 0;
}