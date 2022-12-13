#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
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
    void do_read_SOCKS4_request()
    {
      auto self(shared_from_this());
      client_socket_.async_read_some(boost::asio::buffer(request_data_, max_length),
          [this, self](boost::system::error_code ec, std::size_t length)
          {
            if (!ec)
            {
                std::cout << "" << std::endl;
                std::cout << "<S_IP>: " << client_socket_.remote_endpoint().address().to_string() << std::endl;
                std::cout << "<S_PORT>: " <<  to_string(client_socket_.remote_endpoint().port()) << std::endl;
                // std::cout << "<D_IP>: " << request_endpoint_.address().to_string() << std::endl;
                // std::cout << "<D_PORT>: " << to_string(request_endpoint_.port()) << std::endl;
                // if (request_data_[1] == 1)
                //     std::cout << "<Command>: CONNECT" << std::endl;
                // else
                //     std::cout << "<Command>: BIND" << std::endl;
                // std::cout << "<Reply>: " << reply << std::endl;
                std::cout << "" << std::endl;
            }
          });
    }

    

    tcp::socket client_socket_;
    enum { max_length = 1024 };
    char request_data_[max_length];
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
        printf("\nsignal_handler: caught signal %d\n", signum);
        if (signum == SIGINT) 
        {
            printf("Ctrl+c Terminated socks_server\n");
            exit(SIGINT);
        }
    }

    void do_accept()
    {
      acceptor_.async_accept(
          [this](boost::system::error_code ec, tcp::socket socket)
          {
            if (!ec)
            {
                signal(SIGINT, signal_handler);
                io_context.notify_fork(boost::asio::io_service::fork_prepare);
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