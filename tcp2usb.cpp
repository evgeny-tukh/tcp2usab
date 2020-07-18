#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <WinSock2.h>
#include <cstdint>
#include <time.h>

extern "C" void *__enclave_config = 0;

void showHelp () {
    printf (
        "USAGE\n"
        "\t-t:<tcpport>\n"
        "\t-u:<udpport>\n"
        "\t-r:<remoteaddr>\n"
    );
}

void wrongArgMsg (char *arg) {
    printf ("Invalid argument '%s'. Use TCP2USB -h to get help information.\n", arg);
    exit (0);
}

void checkComma (char *arg) {
    if (arg[2] != ':')
        wrongArgMsg (arg);
}

SOCKET connectHost (uint32_t udpPort, uint32_t tcpPort, in_addr& remoteHost) {
    SOCKET connection = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKADDR_IN local, remote;

    local.sin_port = 0;
    local.sin_family = AF_INET;
    local.sin_addr.S_un.S_addr = INADDR_ANY;

    remote.sin_addr.S_un.S_addr = remoteHost.S_un.S_addr;
    remote.sin_family = AF_INET;
    remote.sin_port = htons (tcpPort);

    if (bind (connection, (const sockaddr *) & local, sizeof (local)) != S_OK) {
        printf ("Unable to bind listener\n");

        closesocket (connection); exit (0);
    }

    printf ("Waiting for a connection...\n");

    while (connect (connection, (const sockaddr *) & remote, sizeof (remote)) != S_OK);

    return connection;
}

SOCKET openDataDistributor (uint32_t udpPort) {
    SOCKET sender = socket (AF_INET, SOCK_DGRAM, IPPROTO_IP);
    SOCKADDR_IN local;
    uint32_t yes = 1;

    setsockopt (sender, SOL_SOCKET, SO_REUSEADDR, (const char *) & yes, sizeof (yes));

    local.sin_port = 0;
    local.sin_family = AF_INET;
    local.sin_addr.S_un.S_addr = INADDR_ANY;

    if (bind (sender, (const sockaddr *) & local, sizeof (local)) != S_OK) {
        printf ("Unable to bind sender\n");

        closesocket (sender); exit (0);
    }

    setsockopt (sender, SOL_SOCKET, SO_BROADCAST, (const char *) & yes, sizeof (yes));

    return sender;
}

void run (uint32_t udpPort, uint32_t tcpPort, in_addr& remoteHost) {
    SOCKET connection, dataDistributor = openDataDistributor (udpPort);
    SOCKADDR_IN dataDestination;

    dataDestination.sin_addr.S_un.S_addr = INADDR_BROADCAST;
    dataDestination.sin_family = AF_INET;
    dataDestination.sin_port = htons (udpPort);

    while (connection = connectHost (udpPort, tcpPort, remoteHost), connection != INVALID_SOCKET) {
        bool stillConnected;
        time_t lastReceiption = time (0);

        do {
            char buffer [2000];
            char sentence [100];
            time_t now = time (0);

            auto receivedBytes = recv (connection, buffer, sizeof (buffer), 0);

            stillConnected = true;

            if (receivedBytes > 0) {
                lastReceiption = now;
                
                buffer [receivedBytes] = 0;

                uint32_t count = 0;

                auto sendSentence = [dataDistributor, &sentence, &count, &dataDestination] () {
                    sentence [count] = 0;

                    sendto (dataDistributor, sentence, count, 0, (const sockaddr *) & dataDestination, sizeof (dataDestination));
                    printf (sentence);

                    count = 0;
                };

                for (auto i = 0; i < sizeof (buffer) && buffer [i]; ++ i) {
                    if (buffer [i] == '!' || buffer [i] == '$' && count > 0)
                        sendSentence ();

                    sentence [count++] = buffer [i];
                }

                if (count > 0)
                    sendSentence ();
            } else if (receivedBytes < 0) {
                switch (WSAGetLastError ()) {
                    case WSAENOTCONN:
                    case WSAENETRESET:
                    case WSAECONNABORTED:
                    case WSAECONNRESET:
                    case WSAETIMEDOUT:
                        printf ("Connection has been lost.\n");

                        stillConnected = false; break;
                }
            } else {
                if ((now - lastReceiption) > 20) {
                    printf ("Connection has been timed out.\n");

                    stillConnected = false;
                }
            }
        } while (stillConnected);

        closesocket (connection);
    }
}

void main (int argCount, char *args[]) {
    uint32_t tcpPort = 8010, udpPort = 8080;
    in_addr remoteHost = { 0 };

    remoteHost.S_un.S_addr = htonl (inet_addr ("188.120.231.145"));

    printf ("TCP to USB converter tool\n\n");
    
    auto checkArg = [&] (char *arg) {
        switch (toupper (arg[1])) {
            case 'H': {
                showHelp ();
                exit (0);
            }

            case 'T': {
                checkComma (arg);

                tcpPort = atoi (arg + 3); break;
            }

            case 'U': {
                checkComma (arg);

                udpPort = atoi (arg + 3); break;
            }

            case 'R': {
                checkComma (arg);

                remoteHost.S_un.S_addr = inet_addr (arg + 3); break;
            }
        }
    };

    auto showValidateArgs = [&tcpPort, &udpPort, &remoteHost] () {
        bool result = true;

        if (!tcpPort) {
            printf ("TCP port not specified!\n"); result = false;
        }

        if (!udpPort) {
            printf ("UDP port not specified!\n"); result = false;
        }

        if (!remoteHost.S_un.S_addr) {
            printf ("Remote address not specified!\n"); result = false;
        }

        return result;
    };

    for (auto i = 1; i < argCount; ++ i) {
        auto arg = args[i];

        if (arg[0] == '/' || arg[0] == '-') {
            checkArg (arg);
        } else {
            wrongArgMsg (arg);
        }
    }

    if (showValidateArgs ()) {
        WSADATA data;

        memset (& data, 0, sizeof (data));

        WSAStartup (MAKEWORD (2, 0), & data);

        run (udpPort, tcpPort, remoteHost);
    }

    exit (0);
}