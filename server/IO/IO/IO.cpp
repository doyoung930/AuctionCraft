#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <iostream>
#include <vector>
#include <thread>

#include "proto/proto_gen/io_messages.pb.h"

#pragma comment(lib, "Ws2_32.lib")

#define DATA_BUFSIZE 8192

struct IO_DATA {
    OVERLAPPED overlapped;
    WSABUF dataBuf;
    char buffer[DATA_BUFSIZE];
    enum { IO_READ, IO_WRITE } ioType;
    SOCKET socket;
};

// 1. 초기화 및 소켓 설정
// 1-1. Windows Sockets API 초기화
bool InitializeWindowsSockets() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

// 1-2. 리슨 소켓 생성 및 설정
SOCKET CreateListenSocket(int port) {
    SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create listen socket." << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(listenSocket);
        return INVALID_SOCKET;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(listenSocket);
        return INVALID_SOCKET;
    }

    return listenSocket;
}

// 2. IOCP 생성 및 설정
HANDLE CreateCompletionPort() {
    HANDLE completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL) {
        std::cerr << "Failed to create IO Completion Port." << std::endl;
        return NULL;
    }
    return completionPort;
}

// 3. 클라이언트 연결 수락
// 3-1. 클라이언트 연결 동기 수락
// AcceptEx를 사용하여 클라이언트 연결을 비동기적으로 수락하고 IOCP에 연결하는 함수
void AcceptClientConnections(SOCKET listenSocket, HANDLE completionPort) {
    // AcceptEx 함수 포인터 로드
    LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    DWORD dwBytes;
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    if (SOCKET_ERROR == WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                 &GuidAcceptEx, sizeof(GuidAcceptEx),
                                 &lpfnAcceptEx, sizeof(lpfnAcceptEx),
                                 &dwBytes, NULL, NULL)) {
        std::cerr << "WSAIoctl failed to load AcceptEx: " << WSAGetLastError() << std::endl;
        return;
    }

    while (true) {
        SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create a client socket: " << WSAGetLastError() << std::endl;
            continue;
        }

        // AcceptEx 요구 사항에 맞춰 버퍼와 OVERLAPPED 구조체 준비
        char acceptBuffer[(sizeof(SOCKADDR_IN) + 16) * 2]; // AcceptEx에서 사용될 버퍼
        OVERLAPPED overlapped = {}; // OVERLAPPED 구조체 초기화

        // 비동기 연결 수락을 위한 AcceptEx 호출
        BOOL acceptExResult = lpfnAcceptEx(
            listenSocket, clientSocket,
            acceptBuffer, 0,
            sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
            NULL, &overlapped);

        if (acceptExResult == FALSE) {
            int lastError = WSAGetLastError();
            if (lastError != ERROR_IO_PENDING) {
                std::cerr << "AcceptEx failed: " << lastError << std::endl;
                closesocket(clientSocket);
                continue;
            }
        }

        // 수락된 클라이언트 소켓을 IOCP에 연결
        if (CreateIoCompletionPort((HANDLE)clientSocket, completionPort, (ULONG_PTR)clientSocket, 0) == NULL) {
            std::cerr << "Failed to associate client socket with completion port: " << GetLastError() << std::endl;
            closesocket(clientSocket);
            continue;
        }

        // 여기서 추가적인 클라이언트 소켓 설정을 수행할 수 있습니다. 예: setsockopt 호출 등

        // 주의: 실제 비동기 연결 수락 완료 처리는 IOCP를 통해 수행되며,
        // 완료된 연결에 대한 후속 처리(예: 데이터 수신 대기 등)는
        // 별도의 스레드에서 GetQueuedCompletionStatus 함수를 사용하여 관리합니다.
    }
}


// 3-2. 클라이언트 소켓을 IOCP에 연결
bool AssociateSocketWithIOCP(SOCKET clientSocket, HANDLE completionPort) {
    // CreateIoCompletionPort 함수를 호출하여 클라이언트 소켓을 IOCP에 연결합니다.
    // 첫 번째 매개변수로 클라이언트 소켓 핸들을, 두 번째 매개변수로 IOCP 핸들을,
    // 세 번째 매개변수로 이 소켓에 대한 키 값을 지정합니다.
    // 여기서는 클라이언트 소켓 자체를 키 값으로 사용하였습니다.
    // 마지막 매개변수는 동시에 처리할 수 있는 스레드 수를 지정하는데,
    // 이미 IOCP를 생성할 때 지정했으므로 0을 전달합니다.
    if (CreateIoCompletionPort((HANDLE)clientSocket, completionPort, (ULONG_PTR)clientSocket, 0) == NULL) {
        std::cerr << "Failed to associate the client socket with IO Completion Port: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

// 4. 비동기 입출력 처리
// 4-1. 클라이언트로부터 데이터 수신을 위한 비동기 요청 등록
bool PostReceive(IO_DATA* ioData) {
    DWORD flags = 0;
    memset(&(ioData->overlapped), 0, sizeof(OVERLAPPED)); // OVERLAPPED 구조체 초기화
    ioData->dataBuf.len = DATA_BUFSIZE; // 수신 버퍼 크기 지정
    ioData->dataBuf.buf = ioData->buffer; // 수신 버퍼 지정

    // 비동기 수신 요청
    int result = WSARecv(ioData->socket, &(ioData->dataBuf), 1, NULL, &flags, &(ioData->overlapped), NULL);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            std::cerr << "WSARecv failed: " << error << std::endl;
            return false;
        }
    }
    return true;
}

// 4-2. 클라이언트로 데이터 송신을 위한 비동기 요청 등록
void HandleIOCompletion(HANDLE completionPort) {
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OVERLAPPED* overlapped;
    while (GetQueuedCompletionStatus(completionPort, &bytesTransferred, &completionKey, &overlapped, INFINITE)) {
        IO_DATA* ioData = CONTAINING_RECORD(overlapped, IO_DATA, overlapped);
        if (bytesTransferred == 0) {
            std::cerr << "Connection closed by peer." << std::endl;
            closesocket(ioData->socket);
            delete ioData;
            continue;
        }

        if (ioData->ioType == IO_DATA::IO_READ) {
            // 데이터 수신 처리 로직
            // 예: 수신된 데이터를 처리하거나, 다음 수신 작업을 위해 PostReceive 호출
        } else if (ioData->ioType == IO_DATA::IO_WRITE) {
            // 데이터 송신 완료 처리 로직
        }

        // 필요한 추가 처리...
    }
}

void Cleanup(SOCKET listenSocket, HANDLE completionPort) {
    // 열린 소켓 닫기
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
    }

    // IOCP 핸들 닫기
    if (completionPort != NULL) {
        CloseHandle(completionPort);
    }

    // Windows Sockets API 정리
    WSACleanup();
}

int main() {
    const int port = 12345; // 서버 포트 번호

    // Windows Sockets 초기화
    if (!InitializeWindowsSockets()) {
        return 1;
    }

    // 리슨 소켓 생성
    SOCKET listenSocket = CreateListenSocket(port);
    if (listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    // IOCP 생성
    HANDLE completionPort = CreateCompletionPort();
    if (completionPort == NULL) {
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // 리슨 소켓을 IOCP에 연결
    if (!AssociateSocketWithIOCP(listenSocket, completionPort)) {
        Cleanup(listenSocket, completionPort);
        return 1;
    }

    // 클라이언트 연결 수락 및 처리를 위한 스레드 시작
    std::thread acceptThread(AcceptClientConnections, listenSocket, completionPort);

    // IOCP 이벤트 처리를 위한 스레드 풀 생성
    const size_t numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> workerThreads;
    for (size_t i = 0; i < numThreads; ++i) {
        workerThreads.emplace_back(HandleIOCompletion, completionPort);
    }

    // 메인 스레드에서는 사용자 입력을 받아 서버를 종료할 수 있도록 함
    std::cout << "Enter 'quit' to stop the server..." << std::endl;
    std::string command;
    while (std::cin >> command) {
        if (command == "quit") {
            break;
        }
    }

    // 종료 및 정리
    Cleanup(listenSocket, completionPort);
    acceptThread.join();
    for (auto& worker : workerThreads) {
        worker.join();
    }

    return 0;
}


// 다음은 protobuf 메시지를 직렬화하고 역직렬화하는 예제입니다.
// <수신하는 경우> 역직렬화
// io::echo msg;
// msg.set_text("Hello, world!");
// msg.ParseFromArray(buffer, bufferSize);
// std::cout << "Received message: " << msg.text() << std::endl;

// <클라이언트로 전송하는 경우> 직렬화 후 전송
// tutorial::MyMessage msg;
// msg.set_text("Hello, World!");
// size_t size = msg.ByteSizeLong();
// void* buffer = malloc(size);
// msg.SerializeToArray(buffer, size);
//
// // buffer를 네트워크를 통해 전송
