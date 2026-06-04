#pragma once

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
