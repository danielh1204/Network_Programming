#include "np_single_proc.h"

int main(int argc, char *argv[])
{
	if (argc != 2)
		return 0;

	for (int i = 0; i < 30; i++)
		clear_usr(i);
	
	working = 0;
	usrpps.clear();
	FD_ZERO(&afds);
	FD_ZERO(&rfds);
	unsigned short ptnum = (unsigned short)atoi(argv[1]);
	struct timeval timeval = {0, 10};
	
	int msock;
	if ((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		cerr << "Fail to create socket.\n";
		exit(0);
	}
	const int enable = 1;
	if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
    	cerr << "setsockopt(SO_REUSEADDR) failed";
		exit(0);
	}

	struct sockaddr_in sin;
	bzero((char *)&sin, sizeof(sin));
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_family = AF_INET;
    sin.sin_port = htons(ptnum);

	if (bind(msock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		cerr << "Fail to bind.\n";
		exit(0);
	}
	FD_SET(msock, &afds);

	listen(msock, 30);
	while(1)
	{
		memcpy(&rfds, &afds, sizeof(rfds));
		int max = msock;
		for (int i = 0; i < 30; i++)
		{
			if (users[i].ssock > max)
				max = users[i].ssock;
		}

		select(max+1, &rfds, NULL, NULL, &timeval);
		if (FD_ISSET(msock, &rfds))
			greeting(msock);

		for (int i = 0; i < 30; i++)
			NPSHELL(i);
	}
	return 0;
}

void sigchld_handle(int signum)
{
	while(waitpid(-1,&status,WNOHANG) > 0){}
}

void parse_cmdsec(string cmdl)
{
	int begin = 0; 
	int end = 0; 
	int tempcnt = 0; 
	char tempsymb = 0; 
	single_cmd tempc;

	while(1)
	{
		t_writeto_usr = 0;
		t_readfrom_usr = 0;
		begin = cmdl.find_first_of("|!",end);
		if(begin != end)
		{
			tempcnt = 0;
			if(cmdl[begin] == '!')
			{
				if(isdigit(cmdl[begin+1]))
					tempsymb = cmdl[begin];
				else
				{
					tempsymb = 0;
					begin = -1;
				}
			}
			else
				tempsymb = cmdl[begin];

			if(tempsymb == 124)
				tempc.ppt = 1;
			else if(tempsymb == 33)
				tempc.ppt = 2;
			else
				tempc.ppt = 0;

			string t_singlecmd = cmdl.substr(end,begin-end);

			if(tempc.ppt != 0 && isdigit(cmdl[begin+1]))
			{
				begin++;
				int first = begin;
				while(isdigit(cmdl[begin+1]))	
					begin++;
				tempcnt = atoi(cmdl.substr(first,begin-first+1).c_str());		
			}

			tempc.unparsed = cmdl;
			tempc.in_index = 0;
			tempc.out_index = 0;
			tempc.write_targusr = 0;
			tempc.read_srcusr = 0;
			tempc.writef = 0;
			tempc.readf = 0;
			tempc.piped_in = false; 
			tempc.piped_out = false;
			tempc.uppwrite = false;
			tempc.uppread = false;

			if(!t_singlecmd.empty())
			{
				t_singlecmd.erase(0, t_singlecmd.find_first_not_of(" ")); //rid of starting spaces
				t_singlecmd.erase(t_singlecmd.find_last_not_of(" ")+1); //rid of ending spaces
				tempc.setmember(tempsymb, t_singlecmd, tempcnt); 
				tempc.setargs();
				tempc.writef = t_writeto_usr;
				tempc.readf = t_readfrom_usr;

				cmdvec.push_back(tempc);
			}
		}

		if(begin == string::npos)
			break;
		
		end = begin+1;
	}
}

void create_pipes(int ind)
{
	ProcPipe temp_pp;
	temp_pp.cnt = 0;
	if(cmdvec[ind].nppcnt > 0 && ind == cmdvec.size()-1)
		temp_pp.cnt = cmdvec[ind].nppcnt;

	if(cmdvec[ind].symb == 124)
		cmdvec[ind].ppt = 1;
	else if(cmdvec[ind].symb == 33)
		cmdvec[ind].ppt = 2;
	else
		cmdvec[ind].ppt = 0;

	if(temp_pp.cnt > 0)
	{ 
		bool need_new_pipe = 1;
		for(int j=0; j<users[working].ppps.size(); j++)
		{
			if(temp_pp.cnt == users[working].ppps.at(j).cnt)
			{
				need_new_pipe = 0;
				cmdvec[ind].out_index = users[working].ppps.at(j).rw[1];
				break;
			}
		}
		if(need_new_pipe)
		{ //create pipe
			pipe(temp_pp.rw);
			cmdvec[ind].out_index = temp_pp.rw[1]; 
			users[working].ppps.push_back(temp_pp);		
		}
	}
	else
	{ 
		if(cmdvec[ind].ppt != 0)
		{
			temp_pp.cnt = -1;
			pipe(temp_pp.rw);
			cmdvec[ind].out_index = temp_pp.rw[1];
			users[working].ppps.push_back(temp_pp);
		}
	}

	if(cmdvec[ind].ppt != 0)
		cmdvec[ind].piped_out = true;

	if (cmdvec[ind].writef > 0)
	{ //user pipe send
		cmdvec[ind].uppwrite = true;
		cmdvec[ind].write_targusr = cmdvec[ind].writef - 1;
	}

	if (cmdvec[ind].readf > 0)
	{ //user pipe receive
		cmdvec[ind].uppread = true;
		cmdvec[ind].read_srcusr = cmdvec[ind].readf - 1;
	}
}

void para_fix(char *com[], int ss, int ind)
{
	vector<string> t_args = cmdvec[ind].args;

	for(int j = 0; j < ss; j++)
		com[j] = (char*)t_args.at(j).c_str();

	com[ss] = NULL;
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

void multicast(int *rcvr, string msg)
{
	const char *message = msg.c_str();

	if (rcvr == NULL)
	{ 
		for (int i = 0; i < 30; i++)
		{
			if (users[i].valid)
				write(users[i].ssock, message, sizeof(char)*msg.length());
		}
	} 
	else 
		write(users[*rcvr].ssock, message, sizeof(char)*msg.length());
}

string naming(string str)
{
	if (str == "")
		return "(no name)";
	else 
		return str;
}

void clear_usr(int ind)
{
	users[ind].envv.clear();
	envvar tempenv;
	tempenv.val = "bin:.";
	tempenv.name = "PATH";
	users[ind].envv.push_back(tempenv);

	users[ind].ssock = 0;
	users[ind].ID = 0;
	users[ind].ip_addr = "";
	users[ind].name = "";
	users[ind].valid = false;
	users[ind].fin = true;

	for (int i = 0; i < users[ind].ppps.size(); i++)
	{
		close(users[ind].ppps[i].rw[0]);
		close(users[ind].ppps[i].rw[1]);
	}
	users[ind].ppps.clear();

	for (int i = 0; i < usrpps.size(); i++)
	{
		if ((usrpps[i].writer == ind) || (usrpps[i].reader == ind))
		{
			close(usrpps[i].rw[0]);
			close(usrpps[i].rw[1]);
			usrpps.erase(usrpps.begin()+i);
		}
	}
}

int execution(int ind)
{
	int cmdtp = cmdvec[ind].filetp(ind);
	char* com[1000];

	if(cmdtp == 1) //printenv
	{ 
		char *varname = getenv(cmdvec[ind].args.at(1).data());
		if(varname != NULL)
		{
			string tt(varname);
			tt = tt + '\n';
			multicast(&working, tt);
		}
	}
	else if(cmdtp == 2) //setenv
	{ 
		envvar te;
		te.name = cmdvec[ind].args.at(1);
		te.val = cmdvec[ind].args.at(2);
		users[working].envv.push_back(te);
		setenv(te.name.data(), te.val.data(), 1);
	}
	else if(cmdtp == 3) //exit || EOF
	{ 
		string msg = "*** User '" + naming(users[working].name) + "' left. ***\n";
		multicast(NULL, msg);
		logout = true;
	}
	else if(cmdtp == 4) //who
	{ 
		string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
		for (int i = 0; i < 30; i++){
			if (users[i].valid)
			{
				msg += to_string(users[i].ID + 1) + "\t" + naming(users[i].name) + "\t" + users[i].ip_addr;
				if (users[i].ID == working)
					msg += "\t<-me";
				
				msg += "\n";
			}
		}
		multicast(&working, msg);
	}
	else if(cmdtp == 5) //tell
	{ 
		string msg = "";
		for(int j=2; j<cmdvec[ind].args.size();j++)
		{
			if(j != cmdvec[ind].args.size() -1)
				msg += cmdvec[ind].args.at(j) + " ";
			else
				msg += cmdvec[ind].args.at(j);
		}

		int rcvr = stoi(cmdvec[ind].args.at(1))-1;

		if (users[rcvr].valid)
		{
			msg = "*** " + naming(users[working].name) + " told you ***: " + msg + "\n";
			multicast(&rcvr, msg);
		} 
		else 
		{
			msg = "*** Error: user #" + to_string(rcvr + 1) + " does not exist yet. ***\n";
			multicast(&working, msg);
		}

	}
	else if(cmdtp == 6) //yell
	{ 
		string t = "";
		for(int j = 1; j < cmdvec[ind].args.size(); j++)
		{
			if(j != cmdvec[ind].args.size() -1)
				t += cmdvec[ind].args.at(j) + " ";
			else
				t += cmdvec[ind].args.at(j);
		}

		string msg = "*** " + naming(users[working].name) + " yelled ***: " + t + "\n";
		multicast(NULL, msg);
	}
	else if(cmdtp == 7) //name
	{ 
		string tnm = cmdvec[ind].args.at(1).c_str();
		for (int j = 0; j < 30; j++)
		{
			if (j == working)
				continue;
			if (users[j].valid && users[j].name == tnm)
			{
				string msg = "*** User '" + tnm + "' already exists. ***\n";
				multicast(&working, msg);
				return 1;
			}
		}

		users[working].name = tnm;
		string msg = "*** User from " + users[working].ip_addr 
				+ " is named '" + users[working].name + "'. ***\n";
		multicast(NULL, msg);
	}
	else if(cmdtp == 8)
	{
	redo:
		pid_t cpid = fork();			

		if(cpid < 0)
		{
			while(waitpid(-1,&status,WNOHANG) > 0){}
			goto redo;
		}

		if(cpid == 0) //child
		{ 
			if(cmdvec[ind].piped_in)
				dup2(cmdvec[ind].in_index, 0);
			if(cmdvec[ind].piped_out)
			{ 
				if(cmdvec[ind].ppt != 2)//ppt 0, 1
				{ 
					dup2(cmdvec[ind].out_index, 1); 
					dup2(users[working].ssock, 2);
				}
				else if(cmdvec[ind].ppt == 2)
				{ 
					dup2(cmdvec[ind].out_index, 1);
					dup2(cmdvec[ind].out_index, 2);
				}
			}
			else
			{
				dup2(users[working].ssock, 1);
				dup2(users[working].ssock, 2);
			}			

			for(int j=0; j<users[working].ppps.size(); j++)
			{
				close(users[working].ppps.at(j).rw[0]);
				close(users[working].ppps.at(j).rw[1]);
			}
			for (int j=0; j<usrpps.size(); j++)
			{
				close(usrpps[j].rw[0]);
				close(usrpps[j].rw[1]);
			}

			int findi = findinvec(cmdvec[ind].args,">");
			// cerr << "INDEX IS: " << index << endl;
			// for(auto ha: cmdvec[i].args) cerr << ha << ' ' << endl;
			if(findi == -1 || cmdvec[ind].uppwrite)
			{ 
				if(cmdvec[ind].ppt == 0)
					para_fix(com, cmdvec[ind].args.size(), ind);
				else
					para_fix(com, cmdvec[ind].args.size(), ind);
			}
			else
			{
				string file = cmdvec[ind].args.at(cmdvec[ind].args.size()-1).data();
				int red = open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC ,0600);
				dup2(red, 1);
				dup2(red, 2);
				close(red);
				para_fix(com, findi, ind);
			}

			if(execvp(com[0], com) == -1)
				cerr << "Unknown command: [" << com[0] << "]." << endl;
			exit(1);
		}
		else
		{
			if(cmdvec[ind].piped_in)
			{
				for(int j=0; j<users[working].ppps.size(); j++)
				{
					if(users[working].ppps.at(j).rw[0] == cmdvec[ind].in_index)
					{
						close(users[working].ppps.at(j).rw[0]);
						close(users[working].ppps.at(j).rw[1]);
						users[working].ppps.erase(users[working].ppps.begin()+j);
						break;
					}
				}
				for (int j = 0; j < usrpps.size(); j++)
				{
					if (usrpps[j].rw[0] == cmdvec[ind].in_index)
					{
						close(usrpps[j].rw[0]);
						close(usrpps[j].rw[1]);
						usrpps.erase(usrpps.begin() + j);
						break;
					}
				}
			}
			if(cmdvec[ind].piped_out)//output piped
			{ 
				if(cmdvec[ind].uppwrite || cmdvec[ind].uppread)
					waitpid(cpid, &status, 0);
				else
				{
					if(cmdvec[ind].nppcnt > 0)
						waitpid(cpid, &status, 0);
					else 
						waitpid(-1, &status, WNOHANG);
				}
			}
			else //no pipe
				waitpid(cpid, &status, 0);
		}
	}
	return 1;
}

bool isthere(int read, int write, int &ind)
{
	bool ret = false;
	for (int i = 0; i < usrpps.size(); i++)
	{
		if (usrpps[i].writer == read && usrpps[i].reader == write)
		{
			ret = true;
			ind = i;
			break;
		}
	}
	return ret;
}

void create_usr_pipe(int i)
{
	int index;

	if (cmdvec[i].uppread)
	{
		if (cmdvec[i].read_srcusr < 0 || cmdvec[i].read_srcusr > 29 || !users[cmdvec[i].read_srcusr].valid)
		{
			string cc = "*** Error: user #" + to_string(cmdvec[i].read_srcusr + 1) + " does not exist yet. ***\n";
			multicast(&working, cc);
			cmdvec[i].piped_in = true;
			cmdvec[i].in_index = blackhole;
		} 
		else 
		{
			if (isthere(cmdvec[i].read_srcusr, working, index))
			{
				string cc = "*** " + naming(users[working].name) + " (#" + to_string(working + 1) + ") just received from "
					 + naming(users[cmdvec[i].read_srcusr].name) + " (#" + to_string(cmdvec[i].read_srcusr + 1) + ") by '" + cmdvec[i].unparsed + "' ***\n";
				multicast(NULL, cc);
				cmdvec[i].piped_in = true;
				cmdvec[i].in_index = usrpps[index].rw[0];
			}
			else 
			{
				string cc = "*** Error: the pipe #" + to_string(cmdvec[i].read_srcusr + 1) + "->#" + to_string(working + 1) + " does not exist yet. ***\n";
				multicast(&working, cc);
				cmdvec[i].piped_in = true;
				cmdvec[i].in_index = blackhole;
			}
		}
	}

	if (cmdvec[i].uppwrite)
	{
		if (cmdvec[i].write_targusr < 0 || cmdvec[i].write_targusr > 29 || !users[cmdvec[i].write_targusr].valid)
		{
			string cc = "*** Error: user #" + to_string(cmdvec[i].write_targusr + 1) + " does not exist yet. ***\n";
			multicast(&working, cc);
			cmdvec[i].piped_out = true;
			cmdvec[i].out_index = blackhole;
		} 
		else 
		{
			if (isthere(working, cmdvec[i].write_targusr, index))
			{
				string cc = "*** Error: the pipe #" + to_string(working + 1) + "->#" + to_string(cmdvec[i].write_targusr + 1) + " already exists. ***\n";
				multicast(&working, cc);
				cmdvec[i].piped_out = true;
				cmdvec[i].out_index = blackhole;
			} 
			else
			{
				string cc = "*** " + naming(users[working].name) + " (#" + to_string(working + 1) + ") just piped '" + cmdvec[i].unparsed + "' to "
					 + naming(users[cmdvec[i].write_targusr].name) + " (#" + to_string(cmdvec[i].write_targusr + 1) + ") ***\n";
				multicast(NULL, cc);

				usrpipe user_nPipe;
				user_nPipe.writer = working;
				user_nPipe.reader = cmdvec[i].write_targusr;
				pipe(user_nPipe.rw);
				usrpps.push_back(user_nPipe);
				cmdvec[i].piped_out = true;
				cmdvec[i].out_index = user_nPipe.rw[1];
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

void NPSHELL(int pt) 
{	
	working = pt;
	if (!users[working].valid)
		return;
	else
	{
		clearenv();
		for (int i = 0; i < users[working].envv.size(); i++)
			setenv(users[working].envv[i].name.data(), users[working].envv[i].val.data(), 1);
		signal(SIGCHLD, sigchld_handle);
	}

	char buf[15000];
	string cmdline;

	if (users[working].fin)
	{
		multicast(&working, "% ");
		users[working].fin = false;
	}

	if (FD_ISSET(users[working].ssock, &rfds) > 0)
	{	
		bzero(buf, sizeof(buf));
		int bytec = read(users[working].ssock, buf, sizeof(buf)); 

		if (bytec > 0)
		{
			cmdline = buf; 
			if (cmdline[cmdline.size()-1] == '\n')
			{
				cmdline = cmdline.substr(0, cmdline.size()-1);
				if (cmdline[cmdline.size()-1] == (char)13)
					cmdline = cmdline.substr(0, cmdline.size()-1);
			}
		}
	}
	else return;

	if(cmdline.empty() || cmdline.find_first_not_of(" ", 0) == string::npos)
	{
		users[working].fin = true;
		return;			
	}

	vector<string> cmdlsec;
	parse_into_sections(cmdline, cmdlsec);
	for(auto ele: cmdlsec)
	{
		parse_cmdsec(ele); 
		for(int i = 0; i < cmdvec.size(); i++)
		{
			create_pipes(i); 
			if(i == 0)
			{
				for(int j = 0; j < users[working].ppps.size(); j++)
				{
					if(users[working].ppps[j].cnt == 0)
					{
						cmdvec[i].in_index = users[working].ppps[j].rw[0];
						cmdvec[i].piped_in = true;
						break;
					}
				}
			}
			else
			{
				for(int j=0; j<users[working].ppps.size(); j++)
				{
					if(users[working].ppps[j].cnt == -1)
					{ 
						cmdvec[i].in_index = users[working].ppps[j].rw[0];
						break;
					}
				}
				cmdvec[i].piped_in = true;
			}
			create_usr_pipe(i);

			if(execution(i) == 0) 
				return;
		}

		for(int i = 0; i < users[working].ppps.size(); i++)
			users[working].ppps[i].cnt--;

		cmdvec.clear();
	}

	users[working].fin = true;

	if(logout){
		logout = false;
		FD_CLR(users[working].ssock, &afds);
		close(users[working].ssock);
		clear_usr(working);
		while(waitpid(-1, &status, WNOHANG) > 0){}
	}	
}

void greeting(int msock)
{
	struct timeval timeval = {0, 10};
	struct sockaddr_in cli_addr;
	socklen_t cli_len = sizeof(cli_addr);

	for (int i = 0; i < 30; i++)
	{
		if (users[i].valid)
			continue;
		else 
		{
			int ssock;
			if ((ssock = accept(msock, (struct sockaddr *)&cli_addr, &cli_len)))
			{
				users[i].valid = true;
				users[i].ssock = ssock;
				users[i].ip_addr = inet_ntoa(cli_addr.sin_addr);
				users[i].ip_addr = users[i].ip_addr+ ":" + to_string(htons(cli_addr.sin_port));
				users[i].ID = i;
				FD_SET(users[i].ssock, &afds);

				string welcome = "****************************************\n** Welcome to the information server. **\n****************************************\n";
				multicast(&users[i].ID, welcome);

				string coming = "*** User '" + naming(users[users[i].ID].name) + "' entered from " + users[users[i].ID].ip_addr + ". ***\n";
				multicast(NULL, coming);
				break;
			}
		}
	}
}


/*
export PATH="$PATH:/home/daniel/NP2/Project 2"
cd ~/NP2/Project\ 2
*/