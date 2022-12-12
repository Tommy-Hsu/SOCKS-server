all:
	g++ -pthread socks_server.cpp -o socks_server
	g++ -pthread console.cpp -o hw4.cgi
clean:
	rm -f socks_server hw4.cgi