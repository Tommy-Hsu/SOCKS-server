#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>


using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;

class session
  : public std::enable_shared_from_this<session>
{
  public:

    session(tcp::socket socket) // class 之建構子，可以為空
    : client_socket_(std::move(socket))
    {
        
    }

    void start()
    {
      do_read_SOCKS4_request();
    }

  private:

    void do_resolve()
    {

      string port, ip;

      port = to_string(ntohs(*((uint16_t*)&request_data_[2])));
      if (request_data_[4] == 0 && request_data_[5] == 0 && request_data_[6] == 0)
          ip = string(&request_data_[9]);
      else
      {
          ip = to_string((uint8_t)request_data_[4]) + '.' + 
               to_string((uint8_t)request_data_[5]) + '.' + 
               to_string((uint8_t)request_data_[6]) + '.' + 
               to_string((uint8_t)request_data_[7]);
      }

      tcp::resolver resolver_(io_context);
      tcp::resolver::query query_(ip, port);
      tcp::resolver::iterator it = resolver_.resolve(query_);
      this->request_endpoint_ = it->endpoint();

    }

    void show_SOCKS_Info(string reply)
    {
      string USERID, DOMAIN_NAME;

      std::cout << "" << std::endl;
      std::cout << "<S_IP>: " << client_socket_.remote_endpoint().address().to_string() << std::endl;
      std::cout << "<S_PORT>: " <<  to_string(client_socket_.remote_endpoint().port()) << std::endl;
      std::cout << "<VN>:" << to_string((uint8_t)request_data_[0]) << std::endl;
      std::cout << "<CD>:" << to_string((uint8_t)request_data_[1]) << std::endl;

      int i;
      for (i = 8; request_data_[i] != '\0'; i++)
      {
          USERID += to_string((uint8_t)request_data_[i]);
      }
      i++;
      for (; request_data_[i] != '\0'; i++)
      {
          DOMAIN_NAME += to_string((uint8_t)request_data_[i]);
      }

      std::cout << "<USERID>:" << USERID << std::endl;
      std::cout << "<DOMAIN_NAME>:" << DOMAIN_NAME << std::endl;
      std::cout << "<D_IP>: " << request_endpoint_.address().to_string() << std::endl;
      std::cout << "<D_PORT>: " << to_string(request_endpoint_.port()) << std::endl;
      std::cout << "<Command>: " << ( (request_data_[1] == 1)? "CONNECT" : "BIND" ) << std::endl;
      std::cout << "<Reply>: " << reply << std::endl;
      std::cout << "" << std::endl;

    }

    int do_connect_DST()
    {
      server_socket_.connect(request_endpoint_);
      //cout<< "do_connect_DST" << std::endl;
      return server_socket_.native_handle();
    }

    void do_write_socks4_reply(int reply, tcp::endpoint endpoint) 
    {
        unsigned short port = endpoint.port();
        unsigned int ip = endpoint.address().to_v4().to_ulong();
        char packet[8];
        packet[0] = 0;
        packet[1] = reply;
        packet[2] = port >> 8 & 0xFF;
        packet[3] = port & 0xFF;
        packet[4] = ip >> 24 & 0xFF;
        packet[5] = ip >> 16 & 0xFF;
        packet[6] = ip >> 8 & 0xFF;
        packet[7] = ip & 0xFF;

        boost::asio::write(client_socket_, boost::asio::buffer(packet, 8));
    }

    void close_socket()
    {
      server_socket_.close();
      client_socket_.close();
    }

    void server_socket_read()
    {
      auto self(shared_from_this());
      server_socket_.async_read_some(boost::asio::buffer(server_data_, max_length),
          [this, self](boost::system::error_code ec, std::size_t length) {
              if (!ec) 
              {
                  client_socket_write(length);
              } 
              else 
              {
                  close_socket();
              }
      });
    }

    void server_socket_write(size_t length)
    {
      auto self(shared_from_this());
      boost::asio::async_write(server_socket_, boost::asio::buffer(client_data_, length),
          [this, self](boost::system::error_code ec, std::size_t length) {
              if (!ec) 
              {
                  memset(client_data_, 0, max_length);
                  client_socket_read();
              } 
              else 
              {
                  close_socket();
              }
      });
    }

    void client_socket_write(size_t length)
    {
      auto self(shared_from_this());
      boost::asio::async_write(client_socket_, boost::asio::buffer(server_data_, length),
          [this, self](boost::system::error_code ec, std::size_t length) {
              if (!ec) 
              {
                  memset(server_data_, 0, max_length);
                  server_socket_read();
              } 
              else 
              {
                  close_socket();
              }
      });
    }

    void client_socket_read()
    {
      auto self(shared_from_this());
      client_socket_.async_read_some(boost::asio::buffer(client_data_, max_length),
          [this, self](boost::system::error_code ec, std::size_t length) {
              if (!ec)
              {
                server_socket_write(length);
              }
              else
              {
                close_socket();
              }

      });
    }


    void do_read_SOCKS4_request()
    {
      auto self(shared_from_this());
      client_socket_.async_read_some(boost::asio::buffer(request_data_, max_length),
          [this, self](boost::system::error_code ec, std::size_t length)
          {
            if (!ec)
            {
                do_resolve();

                if ( request_data_[1] == 1 ) 
                {
                    if( do_connect_DST() < 0 )
                    {

                    }
                    else 
                    {
                      show_SOCKS_Info("Accept");
                      do_write_socks4_reply(90, client_socket_.local_endpoint());
                    }
                }
                else if ( request_data_[1] == 2)
                {

                }

                server_socket_read();
                client_socket_read();
            }
          });
    }

    tcp::socket client_socket_;
    tcp::socket server_socket_{io_context};
    tcp::endpoint request_endpoint_;
    enum { max_length = 1024 };
    char request_data_[max_length] = {0};
    char server_data_[max_length] = {0};
    char client_data_[max_length] = {0};

};

class server
{
  public:
    server(boost::asio::io_context& io_context, short port)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) // server 的建構子
    {
      do_accept();
    }

  private:

    static void signal_handler(int signum) 
    {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {
        }
    }

    void do_accept()
    {
      acceptor_.async_accept(
          [this](boost::system::error_code ec, tcp::socket socket)
          {
            if (!ec)
            {
                signal(SIGCHLD, signal_handler);
                io_context.notify_fork(boost::asio::io_service::fork_prepare);
                //cout<< " do_accept... " << endl;
                pid_t child_pid = fork();

                if(child_pid == 0)
                {
                    io_context.notify_fork(boost::asio::io_context::fork_child);
                    acceptor_.close();
                    std::make_shared<session>(std::move(socket))->start();
                }
                else 
                {
                    io_context.notify_fork(boost::asio::io_context::fork_parent);
                    socket.close();
                    //cout<<" do_accept() ..."<<endl;
                    do_accept();
                }
            }

            do_accept();
          });
    }

    tcp::acceptor acceptor_;
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

    //boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}