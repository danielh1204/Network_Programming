#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <string>
#include <map>
#include <sys/types.h> 
#include <sys/wait.h> 

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::asio::io_context;
using boost::asio::buffer;

io_service ioservice_;

class session : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket) : socket_(std::move(socket))
    {
        env_["REMOTE_ADDR"] = socket_.remote_endpoint().address().to_string();
        env_["REMOTE_PORT"] = to_string(socket_.remote_endpoint().port());
        env_["SERVER_ADDR"] = socket_.local_endpoint().address().to_string();
        env_["SERVER_PORT"] = to_string(socket_.local_endpoint().port());
    }

    void start()
    {
        do_read();
    }


private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    string _str = "";
                    for( int i = 0 ; i < (int)length ; i++ ) 
                        _str += data_[i];

                    parser(_str);
                    setEnvironment();
                    do_write();
                }
            }
        );
    }

    string slicer(string &_str, string del) {
        int pos = _str.find(del);
        string sub = _str.substr( 0, pos );
        _str = _str.substr(pos+del.length());
        return sub;
    }

    void parser( string _str ) 
    {
        string temp = slicer(_str, "\r\n"); //first line
        env_["REQUEST_METHOD"] = slicer(temp, " ");
        string reqURI = slicer(temp, " ");
        reqURI.erase(reqURI.begin());
        env_["REQUEST_URI"] = reqURI;
        source = slicer(reqURI, "?");
        env_["QUERY_STRING"] = reqURI;
        env_["SERVER_PROTOCOL"] = temp;

        int pos = _str.find("Host:" ) + 5;
        while(_str[pos] == ' ' ) pos++;
        int len = 0;
        for(int i = pos ; isalnum(_str[i]) || _str[i] == '.' || _str[i] == ':' ; i++ ) len++;
        env_["HTTP_HOST"] = _str.substr(pos, len);
    }

    void setEnvironment() {
        setenv("REQUEST_METHOD", env_["REQUEST_METHOD"].c_str(), 1 );
        setenv("REQUEST_URI", env_["REQUEST_URI"].c_str(), 1 );
        setenv("QUERY_STRING", env_["QUERY_STRING"].c_str(), 1 );
        setenv("SERVER_PROTOCOL", env_["SERVER_PROTOCOL"].c_str(), 1 );
        setenv("HTTP_HOST", env_["HTTP_HOST"].c_str(), 1 );
        setenv("SERVER_ADDR", env_["SERVER_ADDR"].c_str(), 1 );
        setenv("SERVER_PORT", env_["SERVER_PORT"].c_str(), 1 );
        setenv("REMOTE_ADDR", env_["REMOTE_ADDR"].c_str(), 1) ;
        setenv("REMOTE_PORT", env_["REMOTE_PORT"].c_str(), 1 );
    }

    void do_write()
    {
        auto self(shared_from_this());

        char httpok[1024] = {0};
        snprintf(httpok, 1024, "HTTP/1.1 200 OK\r\n");
        socket_.async_send(buffer(httpok, strlen(httpok)), 
            [this, self](boost::system::error_code ec, std::size_t) 
            {
                if(!ec) 
                {
                    if ( source == "panel.cgi" || source == "printenv.cgi" )
                    {
                        panel();
                    }
                    else if( source == "console.cgi" )
                    {
                        console();
                    }
                    else
                    {
                        other();
                    }
                }
                else 
                {
                    cout << "Error: " << ec.message() << endl;
                }
            }
        );
    }

    void panel() 
    {
        source = "python3 " + source + '\0';
        if(system(source.c_str())<0){
            cerr<<strerror(errno)<<endl;
            exit(EXIT_FAILURE);
        }
    }

    void console()
    {
        if(system("./console.cgi") < 0)
        {
            // system("python3 printenv.cgi");
            cerr << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
    }

    void other() 
    {
        source = "./" + source + '\0';
        if(system(source.c_str())<0)
        {
            cerr<<strerror(errno)<<endl;
            exit(EXIT_FAILURE);
        }
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
    map<string,string> env_;
    string source;
};

class server {
public:
    server(short port) : acceptor_(ioservice_, tcp::endpoint(tcp::v4(), port)), socket_(ioservice_)
    {
        do_accept();
    }

private:
    void do_accept() 
    {
        acceptor_.async_accept(socket_, 
            [this](boost::system::error_code ec)
            {
                if(!ec)
                {
                    //cout << "do accept" << endl;
                    ioservice_.notify_fork(io_service::fork_prepare);
                    pid_t cpid = fork();
                    int status;
                    if (cpid == 0) 
                    {
                        ioservice_.notify_fork(io_service::fork_child);
                        acceptor_.close();

                        dup2(socket_.native_handle(), STDIN_FILENO);
                        dup2(socket_.native_handle(), STDOUT_FILENO);
                        dup2(socket_.native_handle(), STDERR_FILENO);

                        make_shared<session>(move(socket_))->start();
                    }
                    else 
                    {
                        ioservice_.notify_fork(boost::asio::io_context::fork_parent);
                        socket_.close();
                        do_accept();
                    }
                }
                else 
                {
                    cerr << "Accept error: " << ec.message() << std::endl;
                    do_accept();
                }
            }
        );
    }

    tcp::acceptor acceptor_; //msock
    tcp::socket socket_; //ssock
};

int status;

void sigchldhandler(int n)
{
    waitpid(-1,&status,WNOHANG);
}

int main(int argc, char* argv[])
{
    signal(SIGCHLD, sigchldhandler);
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
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