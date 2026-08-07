// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"

#ifndef NETWORK_EXPORTS
#define NETWORK_EXPORTS
#endif

#ifdef NETWORK_EXPORTS
#define NETWORK_API __declspec(dllexport)
#else
#define NETWORK_API __declspec(dllimport)
#endif

// Headers nativos do Windows para Sockets
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <iostream>
#include <cstring>
#include <atomic>

namespace Network {

    // ============================================================================
    // 1. PACKET PROTOCOLS & ENUMS
    // ============================================================================

    // Tipos de pacotes oficiais do Conquer Online (Exemplos principais)
    enum class PacketType : uint16_t {
        MsgRegister = 1001,
        MsgTalk = 1004,
        MsgWalk = 1005,
        MsgUserInfo = 1006,
        MsgItemInfo = 1008,
        MsgAction = 1010, // Muito usado! Jump, Spawn, Die, etc.
        MsgConnect = 1052,
        MsgLoginAuth = 1086,
        MsgPing = 1100
    };

    // Subtipos do MsgAction (Ação genérica do jogo)
    enum class ActionType : uint32_t {
        MapSpawn = 74,
        Jump = 137,
        Teleport = 86,
        Die = 14
    };

    // ============================================================================
    // 2. PACKET BUFFER (WRITER & READER)
    // ============================================================================

    class NETWORK_API Packet {
    public:
        Packet(uint16_t size, PacketType type) {
            m_buffer.resize(size, 0);
            Write<uint16_t>(0, size);
            Write<uint16_t>(2, static_cast<uint16_t>(type));
        }

        Packet(const std::vector<uint8_t>& rawData) : m_buffer(rawData) {}

        // Escrita de dados na memória do pacote
        template<typename T>
        void Write(size_t offset, T value) {
            if (offset + sizeof(T) <= m_buffer.size()) {
                std::memcpy(m_buffer.data() + offset, &value, sizeof(T));
            }
        }

        void WriteString(size_t offset, const std::string& str, size_t maxLength) {
            size_t len = (std::min)(str.length(), maxLength);
            if (offset + len <= m_buffer.size()) {
                std::memcpy(m_buffer.data() + offset, str.c_str(), len);
            }
        }

        // Leitura de dados (Para interpretar os bytes recebidos)
        template<typename T>
        T Read(size_t offset) const {
            T value = 0;
            if (offset + sizeof(T) <= m_buffer.size()) {
                std::memcpy(&value, m_buffer.data() + offset, sizeof(T));
            }
            return value;
        }

        std::string ReadString(size_t offset, size_t length) const {
            if (offset + length <= m_buffer.size()) {
                std::string str(reinterpret_cast<const char*>(m_buffer.data() + offset), length);
                // Limpa lixo de memória até o caractere Nulo (\0)
                return str.substr(0, str.find('\0'));
            }
            return "";
        }

        const std::vector<uint8_t>& GetBuffer() const { return m_buffer; }
        std::vector<uint8_t>& GetBufferRaw() { return m_buffer; }

        uint16_t GetSize() const { return Read<uint16_t>(0); }
        PacketType GetType() const { return static_cast<PacketType>(Read<uint16_t>(2)); }

    private:
        std::vector<uint8_t> m_buffer;
    };

    // ============================================================================
    // 3. CRYPTOGRAPHY INTERFACE (Login & Game)
    // ============================================================================

    class NETWORK_API ICrypto {
    public:
        virtual ~ICrypto() = default;
        virtual void Encrypt(std::vector<uint8_t>& buffer) = 0;
        virtual void Decrypt(std::vector<uint8_t>& buffer) = 0;
    };

    // No Conquer, a criptografia do Login é baseada em RSA/DH (Diffie-Hellman) e Cast256/Blowfish antigo.
    class NETWORK_API LoginCrypto : public ICrypto {
    public:
        void Encrypt(std::vector<uint8_t>& buffer) override {
            // TODO: Implementar TQ Login Encryption
        }
        void Decrypt(std::vector<uint8_t>& buffer) override {
            // TODO: Implementar TQ Login Decryption
        }
    };

    // A criptografia do servidor de Jogo usa Blowfish + RC5 com chaves mutáveis.
    class NETWORK_API GameCrypto : public ICrypto {
    public:
        void Encrypt(std::vector<uint8_t>& buffer) override {
            // TODO: Implementar Blowfish/RC5 Game Encryption
        }
        void Decrypt(std::vector<uint8_t>& buffer) override {
            // TODO: Implementar Blowfish/RC5 Game Decryption
        }
    };

    // ============================================================================
    // 4. TCP SOCKET CLIENT
    // ============================================================================

    // Callback genérico para quando o cliente receber um pacote processado
    using PacketHandlerCallback = std::function<void(std::shared_ptr<Packet>)>;
    using ConnectionStatusCallback = std::function<void(bool isConnected)>;

    class NETWORK_API NetworkClient {
    public:
        NetworkClient() : m_socket(INVALID_SOCKET), m_isConnected(false), m_disconnecting(false) {
            // Inicializa a biblioteca nativa de rede do Windows
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        }

        ~NetworkClient() {
            Disconnect();
            WSACleanup();
        }

        void SetCrypto(std::shared_ptr<ICrypto> crypto) {
            m_crypto = crypto;
        }

        void SetPacketCallback(PacketHandlerCallback callback) {
            m_onPacketReceived = callback;
        }

        void SetStatusCallback(ConnectionStatusCallback callback) {
            m_onStatusChanged = callback;
        }

        bool Connect(const std::string& ip, uint16_t port) {
            m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_socket == INVALID_SOCKET) return false;

            sockaddr_in serverAddr = {};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

            if (connect(m_socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
                return false;
            }

            m_isConnected = true;
            m_disconnecting = false;
            if (m_onStatusChanged) m_onStatusChanged(true);

            // Inicia a thread que fica escutando o servidor para não travar o jogo
            m_recvThread = std::thread(&NetworkClient::ReceiveLoop, this);
            return true;
        }

        void Disconnect() {
            if (!m_isConnected) return;

            m_disconnecting = true;
            m_isConnected = false;

            if (m_socket != INVALID_SOCKET) {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
            }

            if (m_recvThread.joinable()) {
                m_recvThread.join();
            }

            if (m_onStatusChanged) m_onStatusChanged(false);
        }

        void Send(Packet& packet) {
            if (!m_isConnected) return;

            // Encripta o buffer antes de enviar pra rede
            if (m_crypto) {
                m_crypto->Encrypt(packet.GetBufferRaw());
            }

            const auto& buffer = packet.GetBuffer();
            send(m_socket, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
        }

    private:
        void ReceiveLoop() {
            const int BUFFER_SIZE = 8192;
            std::vector<uint8_t> recvBuffer(BUFFER_SIZE);
            std::vector<uint8_t> packetDataQueue; // Buffer cumulativo caso o pacote chegue cortado (TCP Fragmentation)

            while (m_isConnected && !m_disconnecting) {
                int bytesRead = recv(m_socket, reinterpret_cast<char*>(recvBuffer.data()), BUFFER_SIZE, 0);

                if (bytesRead > 0) {
                    // Adiciona os bytes novos ao fim da nossa fila cumulativa
                    packetDataQueue.insert(packetDataQueue.end(), recvBuffer.begin(), recvBuffer.begin() + bytesRead);

                    // Descriptografa tudo o que recebemos de novo na fila
                    if (m_crypto) {
                        m_crypto->Decrypt(packetDataQueue);
                    }

                    // Processa os pacotes montados
                    ProcessPacketQueue(packetDataQueue);
                }
                else if (bytesRead == 0 || (bytesRead == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                    // Servidor fechou a conexão ou erro de rede (Internet caiu)
                    Disconnect();
                    break;
                }
            }
        }

        void ProcessPacketQueue(std::vector<uint8_t>& queue) {
            // O cabeçalho padrão do Conquer: os 2 primeiros bytes indicam o tamanho total do pacote
            while (queue.size() >= 2) {
                uint16_t packetSize = 0;
                std::memcpy(&packetSize, queue.data(), sizeof(uint16_t));

                // Validação de segurança. Se o pacote for menor que o cabeçalho mínimo, o lixo quebrou a fila
                if (packetSize < 4 || packetSize > 8192) {
                    queue.clear(); // Corrupção de protocolo. Limpa a fila e desiste.
                    break;
                }

                // O pacote não chegou inteiro ainda (espera a próxima leitura do TCP)
                if (queue.size() < packetSize) {
                    break;
                }

                // Extrai o pacote inteiro da fila
                std::vector<uint8_t> singlePacketData(queue.begin(), queue.begin() + packetSize);
                queue.erase(queue.begin(), queue.begin() + packetSize);

                // Dispara o evento pro jogo!
                if (m_onPacketReceived) {
                    auto packet = std::make_shared<Packet>(singlePacketData);
                    m_onPacketReceived(packet);
                }
            }
        }

        SOCKET m_socket;
        std::atomic<bool> m_isConnected;
        std::atomic<bool> m_disconnecting;
        std::thread m_recvThread;

        std::shared_ptr<ICrypto> m_crypto;
        PacketHandlerCallback m_onPacketReceived;
        ConnectionStatusCallback m_onStatusChanged;
    };

    // ============================================================================
    // 5. NETWORK MANAGER (Ponto Central de Acesso para o .EXE)
    // ============================================================================

    class NETWORK_API NetworkManager {
    public:
        static NetworkManager& GetInstance() {
            static NetworkManager instance;
            return instance;
        }

        void Initialize() {
            m_client = std::make_unique<NetworkClient>();

            // Aqui é onde nós amarraremos o Callback da rede ao Core::EventDispatcher
            // O código cliente/Game.exe fará a injeção da sua função aqui.
            m_client->SetPacketCallback([this](std::shared_ptr<Packet> packet) {
                this->HandleIncomingPacket(packet);
                });

            m_client->SetStatusCallback([this](bool connected) {
                if (!connected && m_autoReconnect) {
                    // TODO: Lógica de reconexão usando Core::Timer e Core::JobSystem
                }
                });
        }

        bool ConnectToLogin(const std::string& ip, uint16_t port) {
            m_client->SetCrypto(std::make_shared<LoginCrypto>());
            return m_client->Connect(ip, port);
        }

        bool ConnectToGame(const std::string& ip, uint16_t port) {
            m_client->SetCrypto(std::make_shared<GameCrypto>());
            return m_client->Connect(ip, port);
        }

        void Send(Packet& packet) {
            if (m_client) m_client->Send(packet);
        }

        // Permite que o Game.exe injete o despachante de eventos
        void SetGlobalEventCallback(std::function<void(std::shared_ptr<Packet>)> eventDispatcherCall) {
            m_globalDispatcher = eventDispatcherCall;
        }

    private:
        NetworkManager() = default;

        void HandleIncomingPacket(std::shared_ptr<Packet> packet) {
            // Em vez de processar a lógica do Conquer aqui, a rede apenas repassa para frente
            // "Ei Core/Game, chegou um pacote, se vira com os bytes!"
            if (m_globalDispatcher) {
                m_globalDispatcher(packet);
            }
        }

        std::unique_ptr<NetworkClient> m_client;
        std::function<void(std::shared_ptr<Packet>)> m_globalDispatcher;
        bool m_autoReconnect = false;
    };

} // namespace Network