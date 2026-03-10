# Network_Programming
## Project 1
Linux shell
* Number pipe: a pipe that can be used by a process to pass output to a specific later process. 

## Project 2
**Part 1**    
* Modify Project 1 into a tcp server

**Part 2**
* Chat room server
* Listening for clients' connection request
* Single process for handling request
* Clients can broadcast and multicast messages.
* User pipe: clients' process can use such pipes to pass output to a specific client's process

**Part 3**    
* Same as part 2 but using multiprocess for handling requests 

## Project 3
**Part 1**
* http server with synchronous/asynchronous sockets using ``boost.asio``

**Part 2**
* console.cgi
* Pass command to the chat room (Project 2), and output to clients' browser.

**Part 3**
* Modify part 1 and 2 so as to work in windows.

## Project 4
**Part 1**
* SOCKS4/4a server

**Part 2**
* Configure a firewall in HTTP server for remote chat server so that it can only be accessed through SOCKS server.

