#ifndef NETWORK_MONITOR_TESTS_BOOST_MOCK_H
#define NETWORK_MONITOR_TESTS_BOOST_MOCK_H

#include <network-monitor/websocket-client.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/utility/string_view.hpp>

namespace NetworkMonitor {

    /*! \brief Mock the DNS resolver from Boost.Asio.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    class MockResolver {
    public:
        /*! \brief Use this static member in a test to set the error code returned
         *         by async_resolve.
         */
        static boost::system::error_code resolveEc;

        /*! \brief Mock for the resolver constructor
         */
        template <typename ExecutionContext>
        explicit MockResolver(
            ExecutionContext&& context
        ) : context_{ context }
        {
        }

        /*! \brief Mock for resolver::async_resolve
         */
        template <typename ResolveHandler>
        void async_resolve(
            boost::string_view host,
            boost::string_view service,
            ResolveHandler&& handler
        )
        {
            using resolver = boost::asio::ip::tcp::resolver;
            return boost::asio::async_initiate<
                ResolveHandler,
                void(const boost::system::error_code&, resolver::results_type)
            >(
                [](auto&& handler, auto resolver, auto host, auto service) {
                    if (MockResolver::resolveEc) {
                        // Failing branch.
                        boost::asio::post(
                            resolver->context_,
                            boost::beast::bind_handler(
                                std::move(handler),
                                MockResolver::resolveEc,
                                resolver::results_type{} // No resolved endpoints
                            )
                        );
                    }
                    else {
                        // Successful branch.
                        boost::asio::post(
                            resolver->context_,
                            boost::beast::bind_handler(
                                std::move(handler),
                                MockResolver::resolveEc,
                                // Note: The create static method is in the public
                                //       resolver interface but it is not
                                //       documented.
                                resolver::results_type::create(
                                    boost::asio::ip::tcp::endpoint{
                                        boost::asio::ip::make_address(
                                            "127.0.0.1"
                                        ),
                                        443
                                    },
                                    host,
                                    service
                                )
                            )
                        );
                    }
                },
                handler,
                    this,
                    host.to_string(),
                    service.to_string()
                    );
        }

    private:
        // We leave this uninitialized because it does not support a default
        // constructor.
        boost::asio::strand<boost::asio::io_context::executor_type> context_;
    };

    // Out-of-line static member initialization
    inline boost::system::error_code MockResolver::resolveEc{};

    /*! \brief Mock the TCP socket stream from Boost.Beast.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    class MockTcpStream : public boost::beast::tcp_stream {
    public:
        /*! \brief Inherit all constructors from the parent class.
         */
        using boost::beast::tcp_stream::tcp_stream;

        /*! \brief Use this static member in a test to set the error code returned
         *         by async_connect.
         */
        static boost::system::error_code connectEc;

        /*! \brief Mock for tcp_stream::async_connect
         */
        template <typename ConnectHandler>
        void async_connect(
            endpoint_type type,
            ConnectHandler&& handler
        )
        {
            return boost::asio::async_initiate<
                ConnectHandler,
                void(boost::system::error_code)
            >(
                [](auto&& handler, auto stream) {
                    // Call the user callback.
                    boost::asio::post(
                        stream->get_executor(),
                        boost::beast::bind_handler(
                            std::move(handler),
                            MockTcpStream::connectEc
                        )
                    );
                },
                handler,
                    this
                    );
        }
    };

    // Out-of-line static member initialization
    inline boost::system::error_code MockTcpStream::connectEc{};

    // This overload is required by Boost.Beast when you define a custom stream.
    template <typename TeardownHandler>
    void async_teardown(
        boost::beast::role_type role,
        MockTcpStream& socket,
        TeardownHandler&& handler
    )
    {
        return;
    }

    /*! \brief Mock the SSL stream from Boost.Beast.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    template <typename TcpStream>
    class MockSslStream : public boost::beast::ssl_stream<TcpStream> {
    public:
        /*! \brief Inherit all constructors from the parent class.
         */
        using boost::beast::ssl_stream<TcpStream>::ssl_stream;

        /* \brief Use this static member in a test to set the error code returned by
         *        async_handshake.
         */
        static boost::system::error_code handshakeEc;

        /*! \brief Mock for ssl_stream::async_handshake
         */
        template <typename HandshakeHandler>
        void async_handshake(
            boost::asio::ssl::stream_base::handshake_type type,
            HandshakeHandler&& handler
        )
        {
            return boost::asio::async_initiate<
                HandshakeHandler,
                void(boost::system::error_code)
            >(
                [](auto&& handler, auto stream) {
                    // Call the user callback.
                    boost::asio::post(
                        stream->get_executor(),
                        boost::beast::bind_handler(
                            std::move(handler),
                            MockSslStream::handshakeEc
                        )
                    );
                },
                handler,
                    this
                    );
        }
    };

    // Out-of-line static member initialization
    template <typename TcpStream>
    boost::system::error_code MockSslStream<TcpStream>::handshakeEc = {};

    // This overload is required by Boost.Beast when you define a custom stream.
    template <typename TeardownHandler>
    void async_teardown(
        boost::beast::role_type role,
        MockSslStream<MockTcpStream>& socket,
        TeardownHandler&& handler
    )
    {
        return;
    }

    /*! \brief Mock the WebSockets stream from Boost.Beast.
     *
     *  We do not mock all available methods — only the ones we are interested in
     *  for testing.
     */
    template <typename TransportStream>
    class MockWebSocketStream : public boost::beast::websocket::stream<
        TransportStream
    > {
    public:
        /*! \brief Inherit all constructors from the parent class.
         */
        using boost::beast::websocket::stream<TransportStream>::stream;

        /* \brief Use this static member in a test to set the error code returned by
         *        async_handshake.
         */
        static boost::system::error_code handshakeEc;

        /*! \brief Mock for websocket::stream::async_handshake
         */
        template <typename HandshakeHandler>
        void async_handshake(
            boost::string_view host,
            boost::string_view target,
            HandshakeHandler&& handler
        )
        {
            return boost::asio::async_initiate<
                HandshakeHandler,
                void(boost::system::error_code)
            >(
                [](auto&& handler, auto stream, auto host, auto target) {
                    // Call the user callback.
                    boost::asio::post(
                        stream->get_executor(),
                        boost::beast::bind_handler(
                            std::move(handler),
                            MockWebSocketStream::handshakeEc
                        )
                    );
                },
                handler,
                    this,
                    host.to_string(),
                    target.to_string()
                    );
        }
    };

    // Out-of-line static member initialization
    template <typename TransportStream>
    boost::system::error_code MockWebSocketStream<TransportStream>::handshakeEc = {};

    /*! \brief Type alias for the mocked ssl_stream.
     */
    using MockTlsStream = MockSslStream<MockTcpStream>;

    /*! \brief Type alias for the mocked websocket::stream.
     */
    using MockTlsWebSocketStream = MockWebSocketStream<MockTlsStream>;

    /*! \brief Type alias for the mocked WebSocketClient.
     */
    using TestWebSocketClient = WebSocketClient<
        MockResolver,
        MockTlsWebSocketStream
    >;

} // namespace NetworkMonitor

#endif // NETWORK_MONITOR_TESTS_BOOST_MOCK_H