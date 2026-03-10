#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio.hpp>
#include <string>
#include <map>
#include <fstream>

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::asio::io_context;
using boost::asio::buffer;
using namespace boost::asio;
using namespace boost::asio::ip;

io_context ioservice_;
bool panelCalled = 0;
bool paneldonewriting = 0;

typedef struct server_info{
    server_info(string i, string h, string p, string f): id(i), host(h), port(p), file(f){}
    string id;
    string host;
    string port;
    string file;
} server_info;

class session : public std::enable_shared_from_this<session> 
{
private:
    ip::tcp::socket socket_;
    string sessionId;
    string file_;
    enum { max_length = 4096 };
    array<char, max_length> data_;
    ifstream fin;
    vector<string> cmd_str;
    deadline_timer timer;
    
public:
    session(tcp::socket socket, server_info sInfo) :
            socket_( move(socket) ),
            sessionId( sInfo.id ),
            file_(sInfo.file ),
            timer(ioservice_)
    {}

    void start() 
    {
        fin.open("test_case/" + file_ );
        string line;
        while(getline(fin, line))
            cmd_str.push_back(line);
        fin.close();
        do_read();
    }

private:
    string escape(string content )
    {
        string _str = "";
        for ( int i = 0 ; i < content.length(); i++ ) _str += "&#" + to_string(int(content[i])) + ";";
        return _str;
    }
    void output_shell(string sessionstr , string content){
        auto self(shared_from_this());
        string content_= escape(content);
        string temps = "";
        temps = temps + "<script>document.getElementById('" + sessionstr + "').innerHTML += '" + content_ + "';</script>\n"; 
        async_write(socket_, buffer(temps.c_str(), temps.size()), [self](boost::system::error_code ec, std::size_t){}); 
        //cout << "<script>document.getElementById('" << session << "').innerHTML += '" << content_ << "';</script>" << endl;
    }
    void output_command(string sessionstr , string content){
        auto self(shared_from_this());
        string content_= escape(content);
        string temps = "";
        temps = temps + "<script>document.getElementById('" + sessionstr + "').innerHTML += '<b>" + content_ + "</b>';</script>\n"; 
        async_write(socket_, buffer(temps.c_str(), temps.size()), [self](boost::system::error_code ec, std::size_t){}); 
        //cout << "<script>document.getElementById('" << sessionstr << "').innerHTML += '<b>" << content_ << "</b>';</script>" << endl;
    }

    void do_read() //read from npshell
    {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length), 
            [this, self](boost::system::error_code ec, size_t length) 
            {
                if (!ec) 
                {
                    string _str = string(data_.data(), data_.data() + length ); 
                    output_shell(sessionId, _str);
                    if(_str.find("%") != string::npos) do_write(); //prompt is printed
                    do_read();
                }
                else if (ec == boost::asio::error::eof) 
                {
                    string _str = string(data_.data(), data_.data() + length );
                    output_shell(sessionId, _str);
                    _str = "";
                    return;
                }
                else cout << "Error: " << ec.message() << endl;
            }
        );
    }

    void do_write() //write to npshell
    {
        auto self(shared_from_this());
        string output_str = "";
        if (cmd_str.size())
        {
            output_str = cmd_str[0];
            cmd_str.erase(cmd_str.begin());
        }
        output_str += '\n';
        output_command(sessionId, output_str);
        async_write(socket_, buffer(output_str.c_str(), output_str.size()), [self](boost::system::error_code ec, std::size_t){});
    }
};


class npshell : public std::enable_shared_from_this<npshell> 
{
public:
    npshell(server_info serverInfo )
            : socket_(ioservice_ ),
              resolver_(ioservice_),
              query_( serverInfo.host , serverInfo.port ),
              info_(serverInfo )
    {}

    void start()
    {
        do_resolve();
    }

private:
    void do_connect( tcp::resolver::iterator it ) 
    {
        auto self(shared_from_this());
        socket_.async_connect(*it, 
            [this,self](const boost::system::error_code &ec) 
            {
                if (!ec) 
                {
                    make_shared<session>(move(socket_), move(info_))->start();
                }
                else 
                {
                    cout << "Error: " << ec.message() << endl;
                }
            }
        );
    }

    void do_resolve() 
    {
        auto self(shared_from_this());
        resolver_.async_resolve(query_, 
            [this,self](const boost::system::error_code &ec, ::tcp::resolver::iterator it ) 
            {
                if (!ec) 
                {
                    do_connect(it);
                }
                else 
                {
                    cout << "async_resolve error: " << ec.message() << endl;
                }
            }
        );
    }

    tcp::socket socket_;
    tcp::resolver resolver_;
    tcp::resolver::query query_; //hostname:port
    server_info info_;
};

class serv_session : public std::enable_shared_from_this<serv_session>
{
public:
    serv_session(tcp::socket socket) : socket_(std::move(socket))
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
                    if (source == "panel.cgi")
                    {
                        panel();
                        //panelCalled = 1;
                    }
                    else if( source == "console.cgi" )
                    {
                        console();
                    }
                }
                else 
                {
                    cout << "Error: " << ec.message() << endl;
                }
            }
        );
    }

    void createpanelfile()
    {
        ofstream of("p.txt");
        string content = "";
        content = content +
        "import os\n\n"+

        "N_SERVERS = 5\n\n"+

        "FORM_METHOD = 'GET'\n"+
        "FORM_ACTION = 'console.cgi'\n\n"+

        "TEST_CASE_DIR = 'test_case'\n"+
        "try:\n"+
        "    test_cases = sorted(os.listdir(TEST_CASE_DIR))\n"+
        "except:\n"+
        "    test_cases = []\n"+
        "test_case_menu = ''.join([f'<option value=\"{test_case}\">{test_case}</option>' for test_case in test_cases])\n\n"+

        "DOMAIN = 'cs.nctu.edu.tw'\n"+
        "hosts = [f'nplinux{i + 1}' for i in range(12)]\n"+
        "host_menu = ''.join([f'<option value=\"{host}.{DOMAIN}\">{host}</option>' for host in hosts])\n\n"+

        "print('Content-type: text/html', end='\\r\\n\\r\\n')\n\n"+

        "print('''\n"+
        "<!DOCTYPE html>\n"+
        "<html lang=\"en\">\n"+
        "  <head>\n"+
        "    <title>NP Project 3 Panel</title>\n"+
        "    <link\n"+
        "      rel=\"stylesheet\"\n"+
        "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"+
        "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"+
        "      crossorigin=\"anonymous\"\n"+
        "    />\n"+
        "    <link\n"+
        "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"+
        "      rel=\"stylesheet\"\n"+
        "    />\n"+
        "    <link\n"+
        "      rel=\"icon\"\n"+
        "      type=\"image/png\"\n"+
        "      href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"\n"+
        "    />\n"+
        "    <style>\n"+
        "      * {\n"+
        "        font-family: 'Source Code Pro', monospace;\n"+
        "      }\n"+
        "    </style>\n"+
        "  </head>\n"+
        "  <body class=\"bg-secondary pt-5\">''', end='')\n\n"+

        "print(f'''\n"+
        "    <form action=\"{FORM_ACTION}\" method=\"{FORM_METHOD}\">\n"+
        "      <table class=\"table mx-auto bg-light\" style=\"width: inherit\">\n"+
        "        <thead class=\"thead-dark\">\n"+
        "          <tr>\n"+
        "            <th scope=\"col\">#</th>\n"+
        "            <th scope=\"col\">Host</th>\n"+
        "            <th scope=\"col\">Port</th>\n"+
        "            <th scope=\"col\">Input File</th>\n"+
        "          </tr>\n"+
        "        </thead>\n"+
        "        <tbody>''', end='')\n\n"+

        "for i in range(N_SERVERS):\n"+
        "    print(f'''\n"+
        "          <tr>\n"+
        "            <th scope=\"row\" class=\"align-middle\">Session {i + 1}</th>\n"+
        "            <td>\n"+
        "              <div class=\"input-group\">\n"+
        "                <select name=\"h{i}\" class=\"custom-select\">\n"+
        "                  <option></option>{host_menu}\n"+
        "                </select>\n"+
        "                <div class=\"input-group-append\">\n"+
        "                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"+
        "                </div>\n"+
        "              </div>\n"+
        "            </td>\n"+
        "            <td>\n"+
        "              <input name=\"p{i}\" type=\"text\" class=\"form-control\" size=\"5\" />\n"+
        "            </td>\n"+
        "            <td>\n"+
        "              <select name=\"f{i}\" class=\"custom-select\">\n"+
        "                <option></option>\n"+
        "                {test_case_menu}\n"+
        "              </select>\n"+
        "            </td>\n"+
        "          </tr>''', end='')\n\n"+

        "print('''\n"+
        "          <tr>\n"+
        "            <td colspan=\"3\"></td>\n"+
        "            <td>\n"+
        "              <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>\n"+
        "            </td>\n"+
        "          </tr>\n"+
        "        </tbody>\n"+
        "      </table>\n"+
        "    </form>\n"+
        "  </body>\n"+
        "</html>''', end='')";
        of << content << endl;
    }

    void panel() 
    {
        auto self(shared_from_this());
        createpanelfile();

        source = "python3 p.txt > a.txt" + '\0';
        if(system(source.c_str())<0)
        {
            cerr << strerror(errno) << endl;
            exit(EXIT_FAILURE);
        }
        
        // write a.txt into socket
        string outstr = "";
        ifstream infile("a.txt");
        while(!infile.eof())
        {
            string temp = "";
            getline(infile, temp);
            outstr = outstr + temp;
            outstr += '\n';
        }
        outstr = outstr.substr(0, outstr.size()-1);
        async_write(socket_, buffer(outstr.c_str(), outstr.size()), 
            [self](boost::system::error_code ec, std::size_t){ paneldonewriting = 1; });
    }

    map<string, string> session_parser(string _str) 
    {
        map<string, string> queries;
        int pos = 0;
        while(pos != string::npos)
        {
            pos = _str.find("&");
            string sub = _str.substr( 0, pos );
            int posi = sub.find("=");
            queries[sub.substr( 0, posi )] = sub.substr( posi+1);
            _str = _str.substr( pos+1);
        }
        return queries;
    }
    
    void print_html(map<string, string> info_map )
    {
        auto self(shared_from_this());
        string temps = "";
        temps = temps 
        + "Content-type: text/html" + "\r\n\r\n"
        + "<!DOCTYPE html>\n"
        + "<html lang=\"en\">\n"
        +   "<head>\n" 
        +      "<meta charset=\"UTF-8\" />\n"
        +      "<title>NP Project 3 Console</title>\n"
        +      "<link\n"
        +          "rel=\"stylesheet\"\n"
        +          "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
        +          "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
        +          "crossorigin=\"anonymous\"\n"
        +      "/>\n" 
        +      "<link\n"
        +          "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
        +          "rel=\"stylesheet\"\n"
        +      "/>\n" 
        +      "<link\n" 
        +          "rel=\"icon\"\n" 
        +          "type=\"image/png\"\n"
        +          "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
        +      "/>\n"
        +      "<style>\n"
        +          "* {\n"
        +              "font-family: 'Source Code Pro', monospace;\n"
        +              "font-size: 1rem !important;\n" 
        +          "}\n" 
        +          "body {\n" 
        +              "background-color: #212529;\n"
        +          "}\n"
        +          "pre {\n" 
        +              "color: #cccccc;\n" 
        +          "}\n" 
        +          "b {\n" 
        +              "color: #01b468;\n" 
        +          "}\n"
        +      "</style>\n" 
        +  "</head>\n" 
        +  "<body>\n" 
        +      "<table class=\"table table-dark table-bordered\">\n" 
        +          "<thead>\n" 
        +              "<tr>\n" 
        + "<th scope=\"col\">" + info_map["h0"] + ":" + info_map["p0"] + "</th>\n"  //display hostname:port
        + "<th scope=\"col\">" + info_map["h1"] + ":" + info_map["p1"] + "</th>\n" 
        + "<th scope=\"col\">" + info_map["h2"] + ":" + info_map["p2"] + "</th>\n" 
        + "<th scope=\"col\">" + info_map["h3"] + ":" + info_map["p3"] + "</th>\n" 
        + "<th scope=\"col\">" + info_map["h4"] + ":" + info_map["p4"] + "</th>\n"
        +              "</tr>\n" 
        +          "</thead>\n" 
        +          "<tbody>\n" 
        +              "<tr>\n" 
        +                  "<td><pre id=\"s0\" class=\"mb-0\"></pre></td>\n" 
        +                  "<td><pre id=\"s1\" class=\"mb-0\"></pre></td>\n"
        +                  "<td><pre id=\"s2\" class=\"mb-0\"></pre></td>\n" 
        +                  "<td><pre id=\"s3\" class=\"mb-0\"></pre></td>\n" 
        +                  "<td><pre id=\"s4\" class=\"mb-0\"></pre></td>\n" 
        +              "</tr>\n" 
        +          "</tbody>\n" 
        +      "</table>\n" 
        +  "</body>\n" 
        +"</html>\n";
        async_write(socket_, buffer(temps.c_str(), temps.size()), 
            [self](boost::system::error_code ec, std::size_t){});
    }
    
    void console()
    {
        map<string, string> info_map = session_parser(string(getenv("QUERY_STRING")));
        print_html(info_map);

        for(int i=0;i<5;i++)
        {
            string _sstr = "s" + to_string(i);
            string _hstr = "h" + to_string(i);
            string _pstr = "p" + to_string(i);
            string _fstr = "f" + to_string(i);
            if (info_map[_hstr] != "" && info_map[_pstr] != "" && info_map[_fstr] != "" )
                try{
                    server_info info_(_sstr, info_map[_hstr], info_map[_pstr], info_map[_fstr]);
                    make_shared<npshell>(move(info_))->start();
                }
                catch (exception& e) {
                    cerr << "Exception: " << e.what() << "\n";
                }
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
                    make_shared<serv_session>(move(socket_))->start();
                    if(!panelCalled)
                    {
                        while(!paneldonewriting){};
                        socket_.close();
                        panelCalled = 1;
                    }
                        do_accept();



                    //cout << "do accept" << endl;
                    // ioservice_.notify_fork(io_service::fork_prepare);

                    // if ( fork() == 0 ) 
                    // {
                    //     ioservice_.notify_fork(io_service::fork_child);
                    //     acceptor_.close();

                    //     dup2(socket_.native_handle(), STDIN_FILENO);
                    //     dup2(socket_.native_handle(), STDOUT_FILENO);
                    //     dup2(socket_.native_handle(), STDERR_FILENO);

                    //     make_shared<serv_session>(move(socket_))->start();
                    // }
                    // else 
                    // {
                    //     ioservice_.notify_fork(boost::asio::io_context::fork_parent);
                    //     socket_.close();
                    //     do_accept();
                    // }
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

int main(int argc, char* argv[])
{
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