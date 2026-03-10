#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <utility>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <string>
#include <map>

# define TO_CLIENT 1
# define TO_SERVER 2
# define FROM_CLIENT 1
# define FROM_SERVER 2

# define DEBUG 0

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::asio::io_context;
using boost::asio::buffer;

io_service ioservice_;

class socks_bridge : public std::enable_shared_from_this<socks_bridge> {
public:
    socks_bridge(tcp::socket browser_socket, string ip, string port, unsigned char *reply)
            : upstream(move(browser_socket ) ),
              downstream(ioservice_ ),
              resolver_( ioservice_ ),
              query_( ip, port )
    {
        memcpy(reply_, reply, 8);
    }

    void start()
    {
        do_resolve();
    }

private:
    void do_read( int FROM_XXX )
    {
        auto self(shared_from_this());

        if (FROM_XXX == FROM_CLIENT )
        {
            upstream.async_read_some(buffer(upstream_data, max_length ), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec )
                {
                    do_write(TO_SERVER, length );
                }
                else
                {
#if DEBUG
                    cout << "connect: SOCKS read from CLIENT error! " << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }

        if (FROM_XXX == FROM_SERVER )
        {
            downstream.async_read_some(buffer(downstream_data, max_length ), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec )
                {
                    do_write(TO_CLIENT, length);
                }
                else
                {
#if DEBUG
                    cout << "connect: SOCKS read from SERVER error!" << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }
    }

    void do_write(int TO_XXX, size_t length)
    {
        auto self(shared_from_this());
        if (TO_XXX == TO_SERVER )
        {
            downstream.async_send(buffer(upstream_data, length), [this, self, TO_XXX, length](boost::system::error_code ec, std::size_t) {
                if ( !ec ) do_read(FROM_CLIENT );
                else{
#if DEBUG
                    cout << "connect: SOCKS send to SERVER error!" << ec.message() << endl;
#endif
                }
            });
        }
        else if (TO_XXX == TO_CLIENT )
        {
            upstream.async_send(buffer(downstream_data, length), [this, self, TO_XXX, length](boost::system::error_code ec, std::size_t) {
                if (!ec ) do_read(FROM_SERVER);
                else{
#if DEBUG
                    cout << "connect: SOCKS send to SERVER error!" << ec.message() << endl;
#endif
                }
            });
        }
    }

    void do_reply()
    {
        auto self(shared_from_this());
        upstream.async_send(buffer(reply_, 8), [this,self](boost::system::error_code ec, std::size_t /* length */) {
            if(!ec) {
#if DEBUG
                for(int i=0;i<8;i++) cout << '|' << (u_int)reply_[i] ;
                cout << '|' << endl;
#endif
                do_read(FROM_CLIENT);
                do_read(FROM_SERVER);
            }
            else {
#if DEBUG
                cout << "socks_bridge do_reply() error: " << ec.message() << endl;
#endif
            }
        });
    }

    void do_connect( tcp::resolver::iterator it )
    {
        auto self(shared_from_this());
        downstream.async_connect(*it, [this,self](const boost::system::error_code &ec) {
            if (!ec) do_reply();
            else {
#if DEBUG
                cout << "socks_bridge Connect error: " << ec.message() << endl;
#endif
            }
        });
    }

    void do_resolve()
    {
        auto self(shared_from_this());
        resolver_.async_resolve( query_, [this,self](const boost::system::error_code &ec, tcp::resolver::iterator it ) {
            if (!ec) do_connect(it);
            else {
#if DEBUG
                cout << "socks_bridge resolve error: " << ec.message() << endl;
#endif
            }
        });
    }

    enum { max_length = 1024 };
    tcp::socket upstream, downstream;
    tcp::resolver resolver_;
    tcp::resolver::query query_;
    array<char, max_length> upstream_data, downstream_data;
    unsigned char reply_[8];
};

class socks_bind : public std::enable_shared_from_this<socks_bind> {

public:
    socks_bind(tcp::socket ftp_client, unsigned char *reply)
            : upstream(move(ftp_client ) ),
              downstream(ioservice_ ),
              acceptor_(ioservice_ )
    {
        memcpy(reply_, reply, 8);
    }

    void start()
    {
        do_bind();
    }

private:
    void do_read( int FROM_XXX ) {
        auto self(shared_from_this());
        if (FROM_XXX == FROM_CLIENT )
        {
            upstream.async_read_some(buffer(upstream_data, max_length ), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec )
                {
                    do_write(TO_SERVER, length );
                }
                else
                {
#if DEBUG
                    cout << "bind: SOCKS read from CLIENT error!" << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }
        if (FROM_XXX == FROM_SERVER )
        {
            downstream.async_read_some(buffer(downstream_data, max_length ), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec )
                {
                    do_write(TO_CLIENT, length);
                }
                else {
#if DEBUG
                    cout << "bind: SOCKS read from SERVER error!" << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }
    }

    void do_write(int TO_XXX, std::size_t length ){
        auto self(shared_from_this());
        if (TO_XXX == TO_SERVER ) {
            downstream.async_send(buffer(upstream_data, length ), [this, self, TO_XXX, length](boost::system::error_code ec, std::size_t send_length) {
                if ( !ec )
                {
                    if (send_length == length ) do_read( FROM_CLIENT );
                    else do_write(TO_XXX, length - send_length );
                }
                else
                {
#if DEBUG
                    cout << "bind: SOCKS send to SERVER error!" << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }
        else if (TO_XXX == FROM_CLIENT )
        {
            upstream.async_send(buffer(downstream_data, length), [this, self, TO_XXX, length](boost::system::error_code ec, std::size_t send_length) {
                if (!ec )
                {
                    if (send_length == length ) do_read(TO_SERVER );
                    else do_write(TO_XXX, length - send_length );
                }
                else
                {
#if DEBUG
                    cout << "bind: SOCKS send to CLIENT error!" << ec.message() << endl;
#endif
                    downstream.close();
                    upstream.close();
                }
            });
        }
    }

    void do_accept()
    {
        auto self(shared_from_this());
        acceptor_.async_accept(downstream, [this,self](const boost::system::error_code &ec)
        {
            if (!ec)
            {
                do_reply( false );
            }
            else {
#if DEBUG
                cout << "socks_bind Accept error: " << ec.message() << endl;
#endif
            }
        });
    }

    void do_reply( bool isFirstReply ){
        auto self(shared_from_this());
        upstream.async_send(buffer(reply_, 8), [this,self,isFirstReply](boost::system::error_code ec, std::size_t /* length */) {
            if(!ec)
            {
                if ( isFirstReply )
                    do_accept();
                else
                {
#if DEBUG
                    cout << "socks_bind do_reply() error: " << ec.message() << endl;
#endif
                    do_read( FROM_CLIENT );
                    do_read( FROM_SERVER );
                }
            }
            else
            {
#if DEBUG
                cout << "socks_bind do_reply() error: " << ec.message() << endl;
#endif
                downstream.close();
                upstream.close();
            }
        });
    }

    void do_bind() {
        auto self(shared_from_this());
        tcp::endpoint endPoint_( tcp::endpoint( tcp::v4(), 0 ));
        acceptor_.open(endPoint_.protocol() );
        acceptor_.set_option(tcp::acceptor::reuse_address(true) );
        acceptor_.bind(endPoint_ );
        acceptor_.listen();
        unsigned short bind_port = acceptor_.local_endpoint().port(); 
        reply_[2] = (unsigned char) ( bind_port / 256 ) ;
        reply_[3] = (unsigned char) ( bind_port % 256) ;

        do_reply( true );
    }

    enum { max_length = 1024 };
    tcp::socket upstream, downstream;
    tcp::acceptor acceptor_;
    array<char, max_length> upstream_data, downstream_data;
    unsigned char reply_[8];
};

class session: public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket): socket_(std::move(socket))
    {
        socks_info["SRCIP"] = socket_.remote_endpoint().address().to_string();
        socks_info["SRCPORT"] = to_string(socket_.remote_endpoint().port());
    }
    void start()
    {
        do_read();
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length),[this, self](boost::system::error_code ec, std::size_t length)
        {
            if (!ec)
            {
                SOCKS4_parser(data_); // IP and PORT
                do_work();
            }
        });
    }

    void do_work()
    {
        auto self(shared_from_this());

        unsigned char socksrpy[8] = {0};
        u_short DSTPORT = stoi(socks_info["DSTPORT"]);
        string DSTIP = socks_info["DSTIP"];
        socksrpy[2] = DSTPORT / 256;
        socksrpy[3] = DSTPORT % 256;

        // check socks.conf
        if(!firewall()){
            reply_info(false);
            // send reply reject
            socksrpy[0] = 4;
            socksrpy[1] = 91;

            socket_.async_send(buffer(socksrpy, 8), [this, self](boost::system::error_code ec, std::size_t)
            {
                if(!ec) {
                    socket_.close();
                }
            });

        }
        else{
            reply_info(true);
            // send reply accept
            socksrpy[0] = 0;
            socksrpy[1] = 90;
            // CONNECT
            if(socks_info["CD"][0] == '1')
            {
                make_shared<socks_bridge>(move(socket_), socks_info["DSTIP"], socks_info["DSTPORT"], move(socksrpy) )->start();
            }
            // BIND
            else if(socks_info["CD"][0] == '2')
            {
                make_shared<socks_bind>(move(socket_), move(socksrpy) )->start();
            }
        }

    }

    string slicer(string &_str, string del) {
        int pos = _str.find(del);
        string sub = _str.substr( 0, pos );
        _str = _str.substr(pos+del.length());
        if(pos == -1) _str = "";
        return sub;
    }

    void SOCKS4_parser(unsigned char* data_) {
        string VN = to_string(data_[0]);
        socks_info["CD"] = to_string(data_[1]);
        socks_info["DSTPORT"] = to_string((int)data_[2] * 256 + (int)data_[3]);
        socks_info["DSTIP"] = to_string(data_[4]) + "." + to_string(data_[5]) + "." + to_string(data_[6]) + "." + to_string(data_[7]);
        socks_info["USERID"] = "";
        int i=8;
        for(;data_[i]!='\0';i++) socks_info["USERID"] += data_[i];
        // SOCKS4A
        i++; //null byte
        socks_info["DOMAIN_NAME"] = "";
        for(;data_[i]!='\0';i++) socks_info["DOMAIN_NAME"] += data_[i];
#if DEBUG
        cout << "USERID:" << socks_info["USERID"] << endl;
        cout << "DOMAIN_NAME:" << socks_info["DOMAIN_NAME"] << endl;
#endif
        string host = (socks_info["DOMAIN_NAME"] == "")? socks_info["DSTIP"] : socks_info["DOMAIN_NAME"];

        io_service IOSV;
        tcp::resolver resolver_(IOSV);
        tcp::resolver::query query_( host, socks_info["DSTPORT"] );
        tcp::endpoint endpoint_ = *resolver_.resolve(query_);
        socks_info["DSTIP"] = endpoint_.address().to_string();
#if DEBUG
        cout << "DSTIP:" << socks_info["DSTIP"] << endl << endl;
#endif
    }

    bool match_IP(string allow_IP)
    {
        string _str = socks_info["DSTIP"];
        string cmp_A = slicer(_str, "."), cmp_B = slicer(allow_IP, ".");
        int i=1;
        while(cmp_A == cmp_B || cmp_B == "*"){
            cmp_A = slicer(_str, ".");
            cmp_B = slicer(allow_IP, ".");
            i++;
            if(_str.empty() || allow_IP.empty()) break;
        }
        return i == 4;
    }

    bool match_cliIP(string allow_IP)
    {
        string _str = socks_info["SRCIP"];
        string cmp_A = slicer(_str, "."), cmp_B = slicer(allow_IP, ".");
        int i=1;
        while(cmp_A == cmp_B || cmp_B == "*"){
            cmp_A = slicer(_str, ".");
            cmp_B = slicer(allow_IP, ".");
            i++;
            if(_str.empty() || allow_IP.empty()) break;
        }
        return i == 4;
    }

    bool firewall()
    {
        bool clipass = false;

        string cliconf;
        ifstream clisocks("./client_socks.conf");
        while (getline (clisocks, cliconf)) {
            slicer(cliconf, " ");
            string allow_method = slicer(cliconf, " ");
            string allow_IP = slicer(cliconf, " ");
            if(((allow_method == "c" && socks_info["CD"] == "1") || (allow_method == "b" && socks_info["CD"] == "2")) && match_cliIP(allow_IP))
               clipass = 1;
        }
        if(!clipass) return false;

        string config;
        ifstream socks_conf("./socks.conf");
        while (getline (socks_conf, config)) {
            slicer(config, " ");
            string allow_method = slicer(config, " ");
            string allow_IP = slicer(config, " ");
            if(((allow_method == "c" && socks_info["CD"] == "1") || (allow_method == "b" && socks_info["CD"] == "2")) && match_IP(allow_IP))
               return true;
        }
        return false;
    }

    void reply_info( bool result ) {
        cout << "<S_IP>: " << socks_info["SRCIP"] << endl;
        cout << "<S_PORT>: " << socks_info["SRCPORT"] << endl;
        cout << "<D_IP>: " << socks_info["DSTIP"] << endl;
        cout << "<D_PORT>: " << socks_info["DSTPORT"] << endl;
        cout << "<Command>: " << (socks_info["CD"] == "2" ? "BIND" : "CONNECT") << endl;
        cout << "<Reply>: " << (result ? "Accept":"Reject") << endl << endl;
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    unsigned char data_[max_length];
    map<string,string> socks_info;
};

class server {

public:
    server(short port)
            : acceptor_(ioservice_, tcp::endpoint(tcp::v4(), port)),
              socket_(ioservice_)
    {
        do_accept();
    }

private:

    void do_accept() {
        acceptor_.async_accept(socket_, [this](boost::system::error_code ec) {
            if (!ec) {
                ioservice_.notify_fork(io_service::fork_prepare);

                if ( fork() == 0 ) {
                    ioservice_.notify_fork(io_service::fork_child);
                    acceptor_.close();
                    make_shared<session>(move(socket_))->start();
                }
                else {
                    ioservice_.notify_fork(boost::asio::io_context::fork_parent);
                    socket_.close();
                    do_accept();
                }
            }
            else {
                cerr << "Accept error: " << ec.message() << std::endl;
                do_accept();
            }
        });
    }

    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_SOCKS_server <port>\n";
            return 1;
        }
        server server_(std::atoi(argv[1]));
        ioservice_.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}