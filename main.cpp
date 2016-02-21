// Lepton thermal camera websocket streamer by Damir Vodenicarevic
//
// Based on the following libpoco example :
	// WebSocketServer.cpp
	// $Id: //poco/1.4/Net/samples/WebSocketServer/src/WebSocketServer.cpp#1 $
	// This sample demonstrates the WebSocket class.
	// Copyright (c) 2012, Applied Informatics Software Engineering GmbH.
	// and Contributors.
	// SPDX-License-Identifier:	BSL-1.0


#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/Net/NetException.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Format.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include "lepton.h"


using Poco::Net::Socket;
using Poco::Net::SocketAddress;
using Poco::Net::ServerSocket;
using Poco::Net::WebSocket;
using Poco::Net::WebSocketException;
using Poco::Net::HTTPRequestHandler;
using Poco::Net::HTTPRequestHandlerFactory;
using Poco::Net::HTTPServer;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPResponse;
using Poco::Net::HTTPServerResponse;
using Poco::Net::HTTPServerParams;
using Poco::Timespan;
using Poco::Timestamp;
using Poco::ThreadPool;
using Poco::Util::ServerApplication;
using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::HelpFormatter;


class PageRequestHandler: public HTTPRequestHandler
	/// Return a HTML document with some JavaScript creating
	/// a WebSocket connection.
{
public:
	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
	{
		response.setChunkedTransferEncoding(true);
		response.setContentType("text/html");
		response.send() << std::ifstream("resp.html").rdbuf();
		//ostr << "    var ws = new WebSocket(\"ws://" << request.serverAddress().toString() << "/ws\");";
	}
};


class WebSocketRequestHandler: public HTTPRequestHandler
	/// Handle a WebSocket connection.
{
public:
	WebSocketRequestHandler(lepton &rlp) : lp(rlp)
	{
	}


	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
	{
		Application& app = Application::instance();
		try
		{
			WebSocket ws(request, response);
			ws.setNoDelay(true); //we are streaming !
			ws.setBlocking(true);
			app.logger().information("WebSocket connection established.");
			uint8_t buffer[1024];
			int flags;
			int n;
			
			for(;;)
			{
				n = ws.receiveFrame((char*)buffer, sizeof(buffer), flags);
				if((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) break;
				uint8_t curimg= buffer[0];

				std::vector<uint8_t> dtasend(1+FRAME_SIZE);
				uint8_t tmpi= lp.grab_image(dtasend.data()+1, curimg);
				dtasend[0]= tmpi;
				
				n= ws.sendFrame((char*)dtasend.data(), dtasend.size(), WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_BINARY);
			}
			app.logger().information("WebSocket connection closed.");
		}
		catch (WebSocketException& exc)
		{
			app.logger().log(exc);
			switch (exc.code())
			{
			case WebSocket::WS_ERR_HANDSHAKE_UNSUPPORTED_VERSION:
				response.set("Sec-WebSocket-Version", WebSocket::WEBSOCKET_VERSION);
				// fallthrough
			case WebSocket::WS_ERR_NO_HANDSHAKE:
			case WebSocket::WS_ERR_HANDSHAKE_NO_VERSION:
			case WebSocket::WS_ERR_HANDSHAKE_NO_KEY:
				response.setStatusAndReason(HTTPResponse::HTTP_BAD_REQUEST);
				response.setContentLength(0);
				response.send();
				break;
			}
		}
	}
private :
	lepton &lp;
};


class RequestHandlerFactory: public HTTPRequestHandlerFactory
{
public:
	RequestHandlerFactory(lepton &rlp) : lp(rlp)
	{
	}

	HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request)
	{
		if(request.find("Upgrade") != request.end() && Poco::icompare(request["Upgrade"], "websocket") == 0)
			return new WebSocketRequestHandler(lp);
		else
			return new PageRequestHandler;
	}
private :
	lepton &lp;
};


class WebSocketServer: public Poco::Util::ServerApplication
	/// The main application class.
	///
	/// This class handles command-line arguments and
	/// configuration files.
	/// Start the WebSocketServer executable with the help
	/// option (/help on Windows, --help on Unix) for
	/// the available command line options.
	///
	/// To use the sample configuration file (WebSocketServer.properties),
	/// copy the file to the directory where the WebSocketServer executable
	/// resides. If you start the debug version of the WebSocketServer
	/// (WebSocketServerd[.exe]), you must also create a copy of the configuration
	/// file named WebSocketServerd.properties. In the configuration file, you
	/// can specify the port on which the server is listening (default
	/// 80)
	///
	/// To test the WebSocketServer you can use any modern web browser (http://localhost:80/).
{
public:
	WebSocketServer(): _helpRequested(false)
	{
	}
	
	~WebSocketServer()
	{
	}

protected:
	void initialize(Application& self)
	{
		loadConfiguration(); // load default configuration files, if present
		ServerApplication::initialize(self);
	}
		
	void uninitialize()
	{
		ServerApplication::uninitialize();
	}

	void defineOptions(OptionSet& options)
	{
		ServerApplication::defineOptions(options);
		
		options.addOption(
			Option("help", "h", "display help information on command line arguments")
				.required(false)
				.repeatable(false));
	}

	void handleOption(const std::string& name, const std::string& value)
	{
		ServerApplication::handleOption(name, value);

		if (name == "help")
			_helpRequested = true;
	}

	void displayHelp()
	{
		HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("A .");
		helpFormatter.format(std::cout);
	}

	int main(const std::vector<std::string>& args)
	{
		if(_helpRequested)
			displayHelp();
		else
		{
			lepton lp;
			unsigned short port = (unsigned short) config().getInt("WebSocketServer.port", 80);
			ServerSocket svs(port); // set-up a server socket
			HTTPServer srv(new RequestHandlerFactory(lp), svs, new HTTPServerParams); // set-up a HTTPServer instance
			srv.start(); // start the HTTPServer
			waitForTerminationRequest(); // wait for CTRL-C or kill
			srv.stop(); // Stop the HTTPServer
		}
		return Application::EXIT_OK;
	}
	
private:
	bool _helpRequested;
};


POCO_SERVER_MAIN(WebSocketServer)


