#include "np_simple.h"

int main(int argc,char* argv[])
{
	//np_shell();
	unsigned short ptnum;
	if(argc == 2)
		ptnum = (unsigned short)atoi(argv[1]);
	else return 0;

	socklen_t cli_len;
	sockaddr_in serv_addr,cli_addr;
	int msock, ssock;
	if((msock = socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		cerr << "Fail to create socket.\n";
		exit(0);
	}

	const int enable = 1;
	if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		cerr << "setsockopt(SO_REUSEADDR) failed";

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(ptnum);
	
   	if(bind(msock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{
    	cerr << "Fail to bind" << endl;
		exit(0);
	}
  
	listen(msock, 1);

	while(1)
	{
		cli_len = sizeof(cli_addr);
		ssock = accept(msock, (sockaddr*)&cli_addr, &cli_len);
		int cpid = fork();
		if(cpid == 0)
		{
			dup2(ssock, 0);
			dup2(ssock, 1);
			dup2(ssock, 2);
			close(msock);
			NPSHELL();
			break;
		}
		else
		{
			close(ssock);
			signal(SIGCHLD, sigchld_handle);
		}
	}
}

void sigchld_handle(int signum)
{
	int status;
	while(waitpid(-1,&status,WNOHANG) > 0){}
}

void parse_cmdsec(string cmdl)
{
	single_cmd tempcmd;
	int begin = 0; 
	int end = 0; 
	int tcnt = 0;  
	char tsym = 0;

	while(1)
	{
		end = cmdl.find_first_of("|!",begin);
		if(begin != end)
		{
			tcnt = 0;
			tsym = cmdl[end];
			if(tsym == 124)
				tempcmd.ppt = 1;
			else if(tsym == 33)
				tempcmd.ppt = 2;

			string tempcmdl = cmdl.substr(begin,end-begin);
			if(isdigit(cmdl[end+1]))
			{
				end++;
				int from = end;
				while(isdigit(cmdl[end+1]))
					end++;
				tcnt = atoi(cmdl.substr(from,end-from+1).c_str());
			}

			tempcmd.in_index = 0;
			tempcmd.out_index = 0;
			tempcmd.piped_in = false;
			tempcmd.piped_out = false;

			if(!tempcmdl.empty())
			{
				tempcmdl.erase(0, tempcmdl.find_first_not_of(" "));
				tempcmdl.erase(tempcmdl.find_last_not_of(" ")+1);
				tempcmd.setmember(tempcmdl, tsym, tcnt); 
				tempcmd.setargs();
				cmdvec.push_back(tempcmd);
			}
		}
		if(end == string::npos)
			break;
		begin = end+1;
	}		
}

void create_pipe(int ind)
{
	ProcPipe temppp;
	temppp.cnt = 0;
	
	if(cmdvec[ind].nppcnt > 0 && ind == cmdvec.size()-1)
		temppp.cnt = cmdvec[ind].nppcnt;
	if(cmdvec[ind].symb == 124)
		cmdvec[ind].ppt = 1;
	else if(cmdvec[ind].symb == 33)
		cmdvec[ind].ppt = 2;
	else
		cmdvec[ind].ppt = 0;

	if(temppp.cnt > 0)
	{
		bool need_new_pipe = true;
		for(int j = 0; j < ppps.size(); j++)
		{
			if(temppp.cnt == ppps.at(j).cnt)
			{
				cmdvec[ind].out_index = ppps.at(j).rw[1];
				need_new_pipe = false;
				break;
			}
		}
		if(need_new_pipe)
		{ 
			pipe(temppp.rw);
			cmdvec[ind].out_index = temppp.rw[1]; //write
			ppps.push_back(temppp);		
		}
	}
	else
	{ 
		if(cmdvec[ind].ppt != 0)
		{
			temppp.cnt = -1;
			pipe(temppp.rw);
			cmdvec[ind].out_index = temppp.rw[1]; //write
			ppps.push_back(temppp);
		}
	}
	if(cmdvec[ind].ppt != 0)
		cmdvec[ind].piped_out = true;
}

void para_fix(char *buf[], int se, int ind)
{
	vector<string> arg = cmdvec[ind].args;
	for(int j = 0; j < se; j++)
		buf[j] = (char*)arg[j].c_str();
	buf[se] = NULL;
}

int findinvec(vector<string> &vec, string target)
{
	int i;
	for(i = 0; i < vec.size(); i++)
		if(vec[i] == target) return i;

	if(i == vec.size())
		return -1;
	else return i;
}

void execution(int i)
{
	int cmdtp = cmdvec[i].filetp();
	char* buf[1000];

	if(cmdtp == 1)
	{ //printenv
		char* envName = getenv(cmdvec[i].args[1].c_str());
		if(envName != NULL)
			cout<< envName <<endl;
	}
	else if(cmdtp == 2)
		setenv(cmdvec[i].args[1].c_str(), cmdvec[i].args[2].c_str(), 1);
	else if(cmdtp == 3)
		exit(0);
	else if(cmdtp == 4)
	{
		int status;
	redo:
		pid_t cpid = fork();			

		if(cpid < 0)
		{
			while(waitpid(-1,&status,WNOHANG) > 0){}
			goto redo;
		}

		if(cpid == 0)
		{
			if(cmdvec[i].piped_in)
				dup2(cmdvec[i].in_index, 0);
			if(cmdvec[i].piped_out)
			{
				dup2(cmdvec[i].out_index, 1); 
				if(cmdvec[i].ppt == 2) 
					dup2(cmdvec[i].out_index, 2);
			}
			for(int j = 0; j < ppps.size(); j++)
			{
				close(ppps[j].rw[0]);
				close(ppps[j].rw[1]);
			}

			int index = findinvec(cmdvec[i].args,">");
			if(index == -1) //not find
				para_fix(buf, cmdvec[i].args.size(), i);
			else
			{
				string file = cmdvec[i].args.back().data();
				int red = open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC ,0600);
				dup2(red, 1);
				close(red);
				para_fix(buf, index, i);
			}

			if(execvp(buf[0], buf) == -1)
				cerr << "Unknown command: [" << buf[0] << "]." << endl;
			exit(1);
		}
		else{
			if(cmdvec[i].piped_in)
			{
				for(int j = 0; j < ppps.size(); j++)
				{
					if(ppps.at(j).rw[0] == cmdvec[i].in_index)
					{
						close(ppps.at(j).rw[0]);
						close(ppps.at(j).rw[1]);
						ppps.erase(ppps.begin()+j);
						break;
					}
				}
			}

			if(cmdvec[i].ppt == 0)
				waitpid(cpid, &status, 0);
			else
			{
				if(cmdvec[i].nppcnt > 0)
					waitpid(cpid, &status, 0);
				else if (cmdvec[i].nppcnt == 0)
					waitpid(-1, &status, WNOHANG);
			}
		}
	}
}

void parse_into_sections(string cmdl, vector<string>& cmdlSection)
{
	int from = 0;
	while (cmdl != "")
	{
		int found = 0;
		for(int i = 0; i < cmdl.size(); ++i)
		{
			int j = i+1;
			if((cmdl[i] == '|' || cmdl[i] == '!') && cmdl[i+1] != ' ')
			{
				while(j < cmdl.size() && cmdl[j] != ' ') j++; //breaks if j out of bound or cmdline[j] is ' '
		 		j--;
		 		string tempss = cmdl.substr(from, j-from+1);
		 		tempss = tempss.substr(tempss.find_first_not_of(" ", 0));
				cmdlSection.push_back(tempss);
				cmdl = cmdl.substr(j+1);
				found = 1;
				break;
			}
		}

		if(!found)
		{
		    cmdl = cmdl.substr(cmdl.find_first_not_of(" ", 0));
			cmdlSection.push_back(cmdl);
			break;
		}
	}
	//for(auto i: cmdlSection) cout << i << endl;
}

int NPSHELL() 
{	
	clearenv();
	setenv("PATH","bin:.", 1);
	signal(SIGCHLD,sigchld_handle); 

	while(1)
	{
		string cmdline;
		cout<<"% ";
		getline(cin,cmdline);

		if(cmdline.back() == '\n') //handle nl
			cmdline = cmdline.substr(0, cmdline.size()-1);
		if(cmdline.back() == 13) //handle carriage return
			cmdline = cmdline.substr(0, cmdline.size()-1);

		if(cmdline.empty())
			continue;
		if(cmdline.find_first_not_of(" ",0) == string::npos)
			continue;

		vector<string> cmdlSection;
		parse_into_sections(cmdline, cmdlSection);
		for(int secind = 0; secind < cmdlSection.size(); ++secind)
		{
			if(cmdlSection[secind].empty())
				continue;
			if(cmdlSection[secind].find_first_not_of(" ",0) == string::npos)
				continue;

			parse_cmdsec(cmdlSection[secind]);
			
			for(int m = 0; m < cmdvec.size(); m++)
			{
				create_pipe(m); //pipe
				if(m == 0)
				{
					for(int n = 0; n < ppps.size(); n++)
					{
						if(ppps[n].cnt == 0)
						{
							cmdvec[m].in_index = ppps[n].rw[0];
							cmdvec[m].piped_in = true;
							break;
						}
					}
				}
				else
				{
					for(int n = 0; n < ppps.size(); n++)
					{
						if(ppps[n].cnt == -1)
						{ 
							cmdvec[m].in_index = ppps[n].rw[0];
							break;
						}
					}
					cmdvec[m].piped_in = true;
				}
				execution(m);  
			}
			for(int i = 0; i < ppps.size(); i++)
				ppps[i].cnt--;
			cmdvec.clear();
		}
	}	
}