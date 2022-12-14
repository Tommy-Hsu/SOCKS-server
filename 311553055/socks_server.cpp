#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <sys/wait.h>
#include <fstream>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/algorithm/string.hpp>


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
      //cout<< "do_resolve()" << endl;
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

    bool pass_firewall()
    {
      ifstream socks_conf("./socks.conf");
      if(socks_conf)
      {
        string permit, op, ip, ip_part[4];
        while (socks_conf >> permit >> op >> ip)
        {
            if ( ((op == "c") && (request_data_[1] != 1)) || ((op == "b") && (request_data_[1] != 2)) )
              continue;
            //cout << permit << " " << op << " " << ip << endl; 
            for (int i = 0; i < 4; i++)
            {
                auto pos = ip.find_first_of('.');
                if (pos != string::npos)
                {
                    ip_part[i] = ip.substr(0, pos);
                    ip.erase(0, pos + 1);
                }
                else
                {
                    ip_part[i] = ip;
                }
            }
            string tmp = client_socket_.remote_endpoint().address().to_string();
            vector<string> src;
            boost::split(src, tmp, boost::is_any_of("."),boost::token_compress_on);
            if ((ip_part[0] == "*" || ip_part[0] == src[0]) &&
                (ip_part[1] == "*" || ip_part[1] == src[1]) &&
                (ip_part[2] == "*" || ip_part[2] == src[2]) &&
                (ip_part[3] == "*" || ip_part[3] == src[3] ))
            {   
                socks_conf.close();
                //cout << " pass_firewall " << endl;
                return true;
            }
        }

        socks_conf.close();
        //cout << " fail_firewoall " << endl;
        return false;
      }
      else
      {
        //std::cout << " socks.conf isn't exited " << std::endl;
        return false;
      }
    }

    void show_SOCKS_Info(string reply)
    {
      string USERID, DOMAIN_NAME;

      std::cout << "" << std::endl;
      std::cout << "<S_IP>: " << client_socket_.remote_endpoint().address().to_string() << std::endl;
      std::cout << "<S_PORT>: " <<  to_string(client_socket_.remote_endpoint().port()) << std::endl;
      // std::cout << "<VN>:" << to_string((uint8_t)request_data_[0]) << std::endl;
      // std::cout << "<CD>:" << to_string((uint8_t)request_data_[1]) << std::endl;

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

      // std::cout << "<USERID>:" << USERID << std::endl;
      // std::cout << "<DOMAIN_NAME>:" << DOMAIN_NAME << std::endl;
      std::cout << "<D_IP>: " << request_endpoint_.address().to_string() << std::endl;
      std::cout << "<D_PORT>: " << to_string(request_endpoint_.port()) << std::endl;
      std::cout << "<Command>: " << ( (request_data_[1] == 1)? "CONNECT" : "BIND" ) << std::endl;
      std::cout << "<Reply>: " << reply << std::endl;
      std::cout << "" << std::endl;

    }

    int do_bind() 
    {
        auto self(shared_from_this());
        // 建立另一個 port 給 datastream
        // 別忘了 bind operation 也有跑 do_resolve() ，因此當然 ftp client 與 socks server 之 datastream 是可以連上的
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0)); // endpoint 是創建一個 socket，而 acceptor 就是把 socket 開啟 accept 模式，隨機分配 ip 與 port， 0 的用意就是尋找可行的
        do_write_socks4_reply(90, acceptor.local_endpoint() , 2); // 給 ftp server , socks server 創建用來 transfer data 的 port \，請 ftp client 自行告知 ftp server
        //中間 ftp client 透過 connect mode 的通道，告訴 ftp server 如何連進 socks server 哪個 port
        //tcp::socket socket_(io_context);
        acceptor.accept(server_socket_); // acceptor accept 把 server_socket 抓來，當有其他 request 進來 socks server ，就是 server_socket_ 去服務了，
        do_write_socks4_reply(90, acceptor.local_endpoint(), 2); //雖然 reply 內容相同，但 ftp client 知道 reply 2nd 的意思
        return server_socket_.native_handle();
    }

    int do_connect_DST()
    {
      server_socket_.connect(request_endpoint_);
      return server_socket_.native_handle();
    }

    void do_write_socks4_reply(int reply, tcp::endpoint endpoint, int to_who) 
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

        //cout << "Reply to " << ((to_who == 1)?"CONNECT: ":"BIND: ") << to_string(packet[0]) << " | " << reply << " | " << port << " | " << endpoint.address().to_string() << endl;

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
                if(request_data_[0] != 4)
                  return;
                do_resolve();
                if(!pass_firewall())
                {
                  show_SOCKS_Info("Reject");
                  do_write_socks4_reply(91, client_socket_.local_endpoint(), request_data_[1]);
                  close_socket();
                   return;
                }
                //cout << to_string((uint8_t)request_data_[1]) << std::endl;
                if ( request_data_[1] == 1 ) 
                {
                    if( do_connect_DST() < 0 )
                    {
                      show_SOCKS_Info("Reject");
                      do_write_socks4_reply(91, client_socket_.local_endpoint(), request_data_[1]);
                      close_socket();
                      return;
                    }
                    else 
                    {
                      show_SOCKS_Info("Accept");
                      do_write_socks4_reply(90, client_socket_.local_endpoint(), request_data_[1]);
                    }
                }
                else if ( request_data_[1] == 2)
                {
                  //cout << "request_data_[1] == 2" << endl;
                    if( do_bind() < 0 )
                    {
                      show_SOCKS_Info("Reject");
                      do_write_socks4_reply(91, client_socket_.local_endpoint(), request_data_[1]);
                      close_socket();
                      return;
                    }
                    else 
                    {
                      show_SOCKS_Info("Accept");
                    }
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