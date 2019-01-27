A basic HTTP Proxy server written in C with basic cacheing and timeout capabilities, cached files are stored in the www/ directory. Stores ip's in an ipcache file, along with creating a cache info text file that stores information on the last time a file was read from the server.

The server is multi-threaded, and should be able to handle multiple clients simultaneously.

Usage: 
Make - create cache files and www directory in the local directory and compile the proxy server
./proxy <port> <timeout> - run the proxy server on port specified, with files cached for time specified by timeout.
