#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include <fcntl.h>

using namespace std;

int t_writeto_usr = 0; //user pipe sender id
int t_readfrom_usr = 0; //user pipe receiver id
int working;
bool logout = false;
int status;
const int blackhole = open("/dev/null", O_RDWR);

class single_cmd{
public:
    string unparsed; 
    string parsed;
    char symb;
    int nppcnt;
    vector<string> args;
    void setmember(char sym, string par, int cct); 
    int filetp(int ind);
    void setargs();
    int in_index, out_index;
    bool piped_in, piped_out;
    bool uppwrite, uppread;
    int write_targusr, read_srcusr;
	int link1;
	int link2;
    int ppt; // 0: no pipe, 1: normal pipe, 2: error pipe
    int writef;
    int readf;
};

class ProcPipe{
public:
    int rw[2];
    int cnt;
};

class envvar{
public:
    string name;
    string val;
};

class usrpipe{
public:
    int writer; //sourceID
    int reader; //targetID
    int rw[2];
};

class usr{
public:
    bool valid;
    bool fin; //check is the user need to be send "% "
    string ip_addr;
    string name;
    vector<ProcPipe> ppps;
    vector<envvar> envv;
    int ssock;
	int svec, svec1;
    int ID;
};

vector<single_cmd> cmdvec;

void single_cmd::setmember(char sym, string par, int cct)
{
	parsed = par;
	symb = sym;
	nppcnt = cct;
}

void single_cmd::setargs()
{
	vector<string> ttargs;
	stringstream strings(parsed);
	string tt;

	while(strings >> tt)
	{
		int start = 0;
		if(tt[0] == '<')
		{ //user pipe rcv
			while(isdigit(tt[start+1]))
				start++;
			t_readfrom_usr = atoi(tt.substr(1,start).c_str());
			continue;
		}
		else if(tt[0] == '>')
		{
			while(isdigit(tt[start+1]))
			{
				//cerr << "F IS: " << f << " and tt[f]: " << (int)tt[f] << endl;
				start++;
			}
			if(start == 0)
				ttargs.push_back(tt);
			else				
				t_writeto_usr = atoi(tt.substr(1,start).c_str());
			continue;
		}
		else
			ttargs.push_back(tt);
	}
	args = ttargs;
}

int single_cmd::filetp(int ind)
{
	string aa = cmdvec.at(ind).args.at(0);

	if(aa == "printenv")
		return 1;
	else if(aa == "setenv")
		return 2;
	else if(aa == "name")
		return 7;
	else if(aa == "who")
		return 4;
	else if(aa == "tell")
		return 5;
	else if(aa == "yell")
		return 6;
	else if(aa == "exit" || aa == "EOF")
		return 3;
	else
		return 8;
}

fd_set afds, rfds;
usr users[30];
vector<usrpipe> usrpps;


void create_pipes(int ind);

void parse_cmdsec(string cmdl);

void para_fix(char *com[], int ss, int ind);

void multicast(int *rcvr, string msg);

int findinvec(vector<string> &vec, string target);

string naming(string str);

void clear_usr(int ind);

void sigchld_handle(int signum);

bool isthere(int read, int write, int &ind);

void create_usr_pipe(int i);

void NPSHELL(int pt);

void parse_into_sections(string cmdl, vector<string>& cmdlSection);

int execution(int ind);

int sock_and_bind(unsigned short ptnum);

void greeting(int msock);