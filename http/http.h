#ifndef OMFG_HTTP_H
#define OMFG_HTTP_H

#include <string>
#include <list>
#include <utility>
#include <memory>
#include "tcp.h"

#define HAS_ZOIDCOM

#ifdef HAS_ZOIDCOM // TODO: Use Zoidcom for async hostname look-up
#include "network_compat.h"
#endif

namespace TCP {

struct Hostent;

}

namespace HTTP {

struct Request : public TCP::Socket {
	enum State {
		SendingData,
		ReadingHeader,
		ReadingData,
		Done,
	};

	Request(int s_, std::string const &header_, std::string const &data_)
		: TCP::Socket(s_), success(false), state(SendingData), dataSender(0), outData(header_ + data_), dataRecieved(0),
		  dataLength(0), concat(true) {}

	// Returns true if this request is done.
	bool think();

	// Safe error retrieval — handles null requests.
	static TCP::Socket::Error getErrorForRequest(Request *req);

	// Get the current request state.
	State getState() const {
		return state;
	}

	std::string data;
	bool success;

  protected:
	State state;
	TCP::Socket::ResumeSend *dataSender;
	std::string outData;
	std::string header;
	size_t dataRecieved;
	size_t dataLength;
	bool concat;

	void addHeader(std::string const &header);
	char const *parseHeaders(char const *b, char const *e);
	void switchState(State newState);
};

struct Host {
	struct Options {
		Options() : hasProxy(false), userAgent("adlib/3 ($Date: 2005/12/16 19:36:03 $)"), changed(true) {}

		Options(std::string proxy_, int proxyPort_)
			: hasProxy(true), proxy(proxy_), proxyPort(proxyPort_), userAgent("adlib/3 ($Date: 2005/12/16 19:36:03 $)"),
			  changed(true) {}

		void setProxy(std::string proxy_, int proxyPort_) {
			changed = true;

			if (proxy_.empty() || proxyPort_ < 0) {
				hasProxy = false;
				return;
			}

			hasProxy = true;
			proxy = proxy_;
			proxyPort = proxyPort_;
		}

		bool hasProxy;
		std::string proxy;
		int proxyPort;
		std::string userAgent;
		bool changed;
	};

	Host(std::string host_, int port_ = 80, Options const &options_ = Options())
		: host(host_), options(options_), port(port_), hp(0) {}

	~Host();

	std::unique_ptr<Request> query(std::string const &command, std::string const &url, std::string const &addHeader,
								   std::string const &data);

	static char hexDigit(int v);
	static std::string urlencode(std::string const &v);
	static std::string urlencode(std::list<std::pair<std::string, std::string>> const &values);
	std::unique_ptr<Request> post(std::string const &url, std::list<std::pair<std::string, std::string>> const &values);
	std::unique_ptr<Request> get(std::string const &url);

	std::string host;
	Options options;
	int port;
	TCP::Hostent *hp;
};

} // namespace HTTP

#endif // OMFG_HTTP_H
