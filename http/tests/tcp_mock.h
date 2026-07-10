// tcp_mock.h
// Mock TCP socket implementation for testing HTTP parsing without real network.
//
// Usage:
//   MockSocket sock;
//   sock.setConnectionState(MockSocket::Connected);
//   sock.injectData("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello");
//   while(!sock.think()) { /* yield to event loop */ }
//   // sock has data now

#ifndef OMFG_HTTP_TCP_MOCK_H
#define OMFG_HTTP_TCP_MOCK_H

#include "tcp.h"
#include <cstring>
#include <algorithm>
#include <string>

namespace TCP
{

// Mock socket that simulates a TCP connection without real network I/O.
// Use this to test HTTP parsing logic in isolation.
class MockSocket : public Socket
{
public:
	enum ConnectionState
	{
		Connecting,
		Connected,
		Disconnected,
		ErrorConnect,
	};

	MockSocket()
		: Socket(-1)
		, m_state(Connecting)
		, m_dataReady(false)
		, m_bytesConsumed(0)
	{
		connecting = false;
		connected = false;
	}

	// Set the simulated connection state.
	void setConnectionState(ConnectionState state)
	{
		m_state = state;
		if(state == Connected)
		{
			connected = true;
			connecting = false;
			resetTimer();
		}
		else if(state == ErrorConnect)
		{
			connected = false;
			connecting = false;
			error = Socket::ErrorConnect;
		}
		else if(state == Disconnected)
		{
			connected = false;
			connecting = false;
			error = Socket::ErrorDisconnect;
		}
		else
		{
			connected = false;
			connecting = false;
		}
	}

	// Inject raw data that will be returned by subsequent reads.
	size_t injectData(const char* data, size_t len)
	{
		if(!data || len == 0)
			return 0;
		m_pendingData.insert(m_pendingData.end(), data, data + len);
		return len;
	}

	size_t injectData(const char* data)
	{
		return injectData(data, std::strlen(data));
	}

	size_t injectData(const std::string& data)
	{
		return injectData(data.data(), data.size());
	}

	// Get the received data buffer.
	const char* getDataBegin() const { return dataBegin; }
	const char* getDataEnd() const { return dataEnd; }
	size_t getDataLength() const { return dataEnd - dataBegin; }

	// Force the socket to become connected.
	void forceConnected()
	{
		m_state = Connected;
		connected = true;
		connecting = false;
		resetTimer();
	}

	// Check if the mock is in a ready-to-read state.
	bool isReady() const
	{
		return m_state == Connected && m_dataReady;
	}

	// Getters for test access.
	bool isConnected() const { return connected; }
	bool isConnecting() const { return connecting; }
	ConnectionState getConnectionState() const { return m_state; }

	// Override think() to use mock data instead of real socket I/O.
	bool think() override
	{
		if(m_state == Connecting)
		{
			m_state = Connected;
			connected = true;
			connecting = false;
			resetTimer();
			return false;
		}

		if(m_state == ErrorConnect)
		{
			error = Socket::ErrorConnect;
			return true;
		}

		if(m_state == Disconnected)
		{
			error = Socket::ErrorDisconnect;
			return true;
		}

		if(!connected)
			return false;

		// think() only handles connection state — data reading is done by readChunk().
		return false;
	}

	// Override readChunk() to use mock data.
	bool readChunk() override
	{
		dataBegin = dataEnd = 0;

		if(!connected)
			return false;

		if(m_state == Disconnected)
		{
			connected = false;
			return true;
		}

		if(!m_pendingData.empty())
		{
			size_t remaining = m_pendingData.size();
			size_t toCopy = std::min(remaining, static_cast<size_t>(bufferSize));
			std::memcpy(staticBuffer, m_pendingData.data(), toCopy);
			dataBegin = staticBuffer;
			dataEnd = staticBuffer + toCopy;
			m_pendingData.erase(0, toCopy);
			resetTimer();
			return false;
		}

		// No more data
		connected = false;
		return true;
	}

public:
	// Public for access from MockRequest in tests
	bool m_dataReady;
	size_t m_bytesConsumed;
	std::string m_pendingData;

private:
	ConnectionState m_state;
};

} // namespace TCP

#endif // OMFG_HTTP_TCP_MOCK_H
