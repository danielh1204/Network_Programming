#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <map>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <utility>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

io_service ioservice_;

typedef struct server_info{
    server_info(string i, string h, string p, string f): id(i), host(h), port(p), file(f){}
    string id;
    string host;
    string port;
    string file;
} server_info;

class npshell : public std::enable_shared_from_this<npshell> {
public:

    npshell(server_info serverInfo, string sh, string sp)
            : socket_(ioservice_ ),
              resolver_(ioservice_),
              query_( serverInfo.host , serverInfo.port ),
              sessionId( serverInfo.id ),
              socks_( sh, sp ),
              source( serverInfo.file )
    {}

    void start()
    {
        do_resolve();
    }

private:

    string slicer(string &_str, string del) {
        int pos = _str.find(del);
        string sub = _str.substr( 0, pos );
        _str = _str.substr(pos+del.length());
        if(pos == -1) _str = "";
        return sub;
    }

    string escape(string content )
    {
        string _str = "";
        for ( int i = 0 ; i < content.length(); i++ ) _str += "&#" + to_string(int(content[i])) + ";";
        return _str;
    }

    void output_shell(string session , string content)
    {
        string content_= escape(content);
        cout << "<script>document.getElementById('" << session << "').innerHTML += '" << content_ << "';</script>" << endl;
    }

    void output_command(string session , string content)
    {
        string content_= escape(content);
        cout << "<script>document.getElementById('" << session << "').innerHTML += '<b>" << content_ << "</b>';</script>" << endl;
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some( buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                string _str(data_);
                output_shell(sessionId, _str);
                if(_str.find("%") != string::npos) do_write();
                do_read();
            }
            else if (ec == boost::asio::error::eof) {
                string _str(data_);
                output_shell(sessionId, _str);
                return;
            }
            else cout << "Error: " << ec.message() << endl;
        });
    }

    void do_write() {
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

    void do_open_test_case()
    {
        auto self(shared_from_this());

        socket_.async_read_some(buffer(data_, 8 ), [this, self](boost::system::error_code ec, size_t length) {
            if(!ec) {
                if(data_[1] == 90) {
                    fin.open( "test_case/" + source );
                    string line;
                    while(getline(fin, line)) cmd_str.push_back(line);
                    fin.close();
                    do_read();
                }
                else socket_.close();
            }
            else socket_.close();
        });
    }

    void do_request(){
        char socksrpy[8];
        tcp::endpoint endpoint_ = *resolver_.resolve(query_);

        socksrpy[0] = 4;
        socksrpy[1] = 1;
        socksrpy[2] = endpoint_.port() / 256;
        socksrpy[3] = endpoint_.port() % 256;

        string IP = endpoint_.address().to_string();
        for(int i = 0; i < 4; i++ )
            socksrpy[4+i] = stoi(slicer(IP, "."));

        write( socket_, buffer(socksrpy, 8 ) );
    }

    void do_connect( tcp::resolver::iterator it )
    {
        auto self(shared_from_this());
        socket_.async_connect(*it, [this,self](const boost::system::error_code &ec) {
            if (!ec)
            {
                do_request();
                do_open_test_case();
            }
            else
            {
                cout << "Error: " << ec.message() << endl;
            }
        });
    }

    void do_resolve()
    {
        auto self(shared_from_this());
        resolver_.async_resolve( socks_, [this,self](const boost::system::error_code &ec, ::tcp::resolver::iterator it ) {
            if (!ec)
            {
                do_connect(it);
            }
        });
    }
    enum { max_length = 1024 };
    tcp::socket socket_;
    tcp::resolver resolver_;
    tcp::resolver::query query_, socks_;
    ifstream fin;
    char data_[max_length];
    string source, sessionId;
    vector<string> cmd_str;
};

map<string, string> parser(string _str) {
    map<string, string> queries;
    int pos = 0;
    while(pos != string::npos){
        pos = _str.find("&");
        string sub = _str.substr( 0, pos );
        int posi = sub.find("=");
        queries[sub.substr( 0, posi )] = sub.substr( posi+1);
        _str = _str.substr( pos+1);
    }
    return queries;
}

void print_html(map<string, string> info_map ){
    cout<<"Content-type: text/html"<<"\r\n\r\n";
    cout<<"<!DOCTYPE html>" <<endl;
    cout<<"<html lang=\"en\">" <<endl;
    cout<<  "<head>" <<endl;
    cout<<      "<meta charset=\"UTF-8\" />" <<endl;
    cout<<      "<title>NP Project 3 Console</title>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<          "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"" <<endl;
    cout<<          "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"" <<endl;
    cout<<          "crossorigin=\"anonymous\"" <<endl;
    cout<<      "/>" << endl;
    cout<<      "<link" << endl;
    cout<<          "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"icon\"" <<endl;
    cout<<          "type=\"image/png\"" <<endl;
    cout<<          "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<style>" <<endl;
    cout<<          "* {" <<endl;
    cout<<              "font-family: 'Source Code Pro', monospace;" <<endl;
    cout<<              "font-size: 1rem !important;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "body {" <<endl;
    cout<<              "background-color: #212529;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "pre {" <<endl;
    cout<<              "color: #cccccc;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "b {" <<endl;
    cout<<              "color: #01b468;" <<endl;
    cout<<          "}" <<endl;
    cout<<      "</style>" <<endl;
    cout<<  "</head>" <<endl;
    cout<<  "<body>" <<endl;
    cout<<      "<table class=\"table table-dark table-bordered\">" <<endl;
    cout<<          "<thead>" <<endl;
    cout<<              "<tr>" <<endl;
    cout << "<th scope=\"col\">" << info_map["h0"] << ":" << info_map["p0"] << "</th>" << endl;
    cout << "<th scope=\"col\">" << info_map["h1"] << ":" << info_map["p1"] << "</th>" << endl;
    cout << "<th scope=\"col\">" << info_map["h2"] << ":" << info_map["p2"] << "</th>" << endl;
    cout << "<th scope=\"col\">" << info_map["h3"] << ":" << info_map["p3"] << "</th>" << endl;
    cout << "<th scope=\"col\">" << info_map["h4"] << ":" << info_map["p4"] << "</th>" << endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</thead>" <<endl;
    cout<<          "<tbody>" <<endl;
    cout<<              "<tr>" <<endl;
    cout<<                  "<td><pre id=\"s0\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s1\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s2\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s3\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s4\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</tbody>" <<endl;
    cout<<      "</table>" <<endl;
    cout<<  "</body>" <<endl;
    cout<<"</html>" <<endl;

}

int main(){
    map<string, string> info_map = parser(string(getenv("QUERY_STRING")));
    print_html(info_map);

    for(int i=0;i<5;i++){
        string _sstr = "s" + to_string(i);
        string _hstr = "h" + to_string(i);
        string _pstr = "p" + to_string(i);
        string _fstr = "f" + to_string(i);
        if (info_map[_hstr] != "" && info_map[_pstr] != "" && info_map[_fstr] != "" )
            try{
                server_info info_(_sstr, info_map[_hstr], info_map[_pstr], info_map[_fstr]);
                make_shared<npshell>(move(info_), info_map["sh"], info_map["sp"])->start();
            }
            catch (exception& e) {
                cerr << "Exception: " << e.what() << "\n";
            }
    }
    ioservice_.run();
}
