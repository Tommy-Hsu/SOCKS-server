#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <vector>
#include <utility>
#include <memory>
#include <regex>
#include <boost/asio/io_context.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;
using namespace std;

#define MAX_SESSION 5
std::string host_name[MAX_SESSION];
std::string port_number[MAX_SESSION];
std::string file_name[MAX_SESSION];
std::vector <std::string> cmds[MAX_SESSION+1];

std::string socks_host;
std::string socks_port;

void Parse_QUERY_STRING()
{
   string q = getenv("QUERY_STRING");
   //cout << q << endl;
   vector<string> query;
	boost::split(query, q, boost::is_any_of("&"), boost::token_compress_on);

   for( vector<string>::iterator it = query.begin(); it != query.end(); ++ it )
   {
      if(it->at(0) == 'h')
      {
         int id = it->at(1)-'0';
         //int id = stoi(it->substr(it->find("h")+1, it->find("=")));
         //cout<<id<<endl;
         host_name[id] = it->substr(it->find("=")+1);
         //cout<<host_name[id]<<endl;
      }
      if(it->at(0) == 'p')
      {
         int id = it->at(1)-'0';
         //int id = stoi(it->substr(it->find("p")+1, it->find("=")));
         //cout<<id<<endl;
         port_number[id] = it->substr(it->find("=")+1);
         //cout<<port_number[id]<<endl;
      }
      if(it->at(0) == 'f')
      {
         int id = it->at(1)-'0';
         //int id = stoi(it->substr(it->find("f")+1, it->find("=")));
         //cout<<id<<endl;
         file_name[id] = it->substr(it->find("=")+1);
         //cout<<file_name[id]<<endl;
      }
      if( (it->at(0) == 's') && it->at(1) == 'h')
      {
         socks_host = it->substr(it->find("=")+1);
         //cout << socks_host << endl;
      }
      if( (it->at(0) == 's') && it->at(1) == 'p')
      {
         socks_port = it->substr(it->find("=")+1);
         //cout << socks_port << endl;
      }
   }
}

void Show_HTML()
{
   string thead = {};
   string tdata = {};

   for(int i = 0; i < 5; i++)
   {
      if(host_name[i].empty())
         break;
      thead += "<th scope=\"col\">" + host_name[i] + ":" + port_number[i] + "</th>\n";
      tdata += "<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>\n";
   }

   printf("Content-type: text/html\r\n\r\n");
   printf(R"(
   
   <!DOCTYPE html>
   <html lang="en">
   <head>
      <meta charset="UTF-8" />
      <title>NP Project 3 Sample Console</title>
      <link
         rel="stylesheet"
         href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
         integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
         crossorigin="anonymous"
      />
      <link
         href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
         rel="stylesheet"
      />
      <link
         rel="icon"
         type="image/png"
         href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
      />
      <style>
         * {
         font-family: 'Source Code Pro', monospace;
         font-size: 1rem !important;
         }
         body {
         background-color: #212529;
         }
         pre {
         color: #cccccc;
         }
         b {
         color: #01b468;
         }
      </style>
   </head>

   <body>
      <table class="table table-dark table-bordered">
         <thead>
         <tr>
            %s
         </tr>
         </thead>
         <tbody>
         <tr>
            %s     
         </tr>
         </tbody>
      </table>
   </body>

   </html>
   )", thead.c_str(), tdata.c_str());
}

class Session : public std::enable_shared_from_this<Session> 
{
    private:

      tcp::socket socket_;
      tcp::resolver resolv_;
      
      enum { max_length = 1024 };
      char data_[max_length];
      std::string host_n;
      std::string host_p;

      tcp::resolver::query query_{ socks_host, socks_port };
      std::string file;
      int session_id;
      int cmd_idx;
        
    public:
        // constructor && some info to init
        Session(boost::asio::io_context& io_context, std::string host_n, std::string host_p, std::string file, int session_id)
        : socket_(io_context)
          , resolv_(io_context)
          , host_n(host_n)
          , host_p(host_p)
          , file(file)
          , session_id(session_id)
          , cmd_idx(0)
        {
            read_cmd_from_txt();
        }
        void start() { do_resolve(); }

    private:

         void replace_string(string &data) 
         {
            boost::replace_all(data, "<", "&lt;");
            boost::replace_all(data, ">", "&gt;");
            boost::replace_all(data, " ", "&nbsp;");
            boost::replace_all(data, "\"", "&quot;");
            boost::replace_all(data, "\'", "&apos;");
            boost::replace_all(data, "\r", "");
            boost::replace_all(data, "\n", "&NewLine;");
            boost::replace_all(data, "<", "&lt;");
         }

        void read_cmd_from_txt()
        {
            //cout<<"session "<< session_id <<" "<< host_n <<" " << host_p <<" " <<file<<endl;

            std::string cc;
            std::ifstream f("./test_case/"+ file);
            if(f)
            {
                while (getline (f, cc))
                {
                    cmds[session_id].push_back(cc);
                }
                f.close();
               //  for (auto it = cmds[session_id].begin(); it != cmds[session_id].end(); ++it) 
               //    cout << *it <<endl;
            }
            else
                std::cout << "<p> can't open " << file << "</p>" << std::endl;
        }

        void do_send_cmd()
        {
            auto self(shared_from_this());
            socket_.async_send(boost::asio::buffer(cmds[session_id][cmd_idx]+"\n"),
               [this, self](boost::system::error_code ec, size_t /* length */) 
               {
                  if (!ec)
                  {
                     cmd_idx++;
                     do_read();
                  } 
               });
        }

        void do_read() 
        {
            //cout<<"success to do_read()"<<endl; //還沒進去 server
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {

                if (!ec)
                {    

                     std::string o = string(data_);
                     memset(data_, 0, sizeof data_);
                     //replace_string(o);

                     if (o.find("%") != std::string::npos)
                     {
                        std::string t = o.substr(0,o.find("%"));
                        replace_string(t);
                        std::cout << "<script>document.getElementById('s" << session_id << "').innerHTML += '" << t <<"';</script>" << std::endl;
                        std::cout.flush();
                        //output_shell(std::to_string(s_id), recv_str.substr(0, recv_str.find("%")));
                        std::cout << "<script>document.getElementById('s" << session_id << "').innerHTML += '<b>" << "% " << "</b>';</script>" << std::endl;
                        std::cout.flush();
                        //output_prompt(std::to_string(s_id));
                        usleep(100000);
                        t = cmds[session_id][cmd_idx];
                        replace_string(t);
                        std::cout << "<script>document.getElementById('s" << session_id << "').innerHTML += '<b>" << t << "&NewLine;</b>';</script>" << std::endl;
                        std::cout.flush();
                        //output_command(std::to_string(s_id), cmds[s_id][cmd_idx]);

                        usleep(100000);
                        do_send_cmd();
                        //usleep(100000);
                    }
                    else
                    {
                        std::string t = o;
                        replace_string(t);
                        std::cout << "<script>document.getElementById('s" << session_id << "').innerHTML += '" << t <<"';</script>" << std::endl;
                        std::cout.flush();
                        do_read();
                        //output_shell(std::to_string(s_id), recv_str);
                    }
                }
                else
                {
                     std::cout << "<script>document.getElementById('s" << session_id << "').innerHTML += '<b>" << "(connection close)" << "&NewLine;</b>';</script>" << std::endl;
                     std::cout.flush();
                    //output_command(std::to_string(s_id), "(connection close)");
                }
            });
        }

        void do_write_SOCKS4_request()
        {
            auto self(shared_from_this());
            char packet[max_length] = {0};
            packet[0] = 4;
            // connect
            packet[1] = 1;
            // DSTport
            packet[2] = (atoi(host_p.c_str()) >> 8) & 0xFF;
            packet[3] = atoi(host_p.c_str()) & 0xFF;
            // DSTIP
            packet[4] = 0;
            packet[5] = 0;
            packet[6] = 0;
            packet[7] = 1;
            // NULL
            packet[8] = 0;
            // DOMAIN_NAME
            memcpy(packet + 9, host_n.c_str(), host_n.size());
            // NULL
            packet[9 + host_n.size()] = 0;

            //auto self(shared_from_this());
            socket_.async_write_some(boost::asio::buffer(packet, 10 + host_n.size()),
            [this, self](boost::system::error_code ec, size_t length)
            {
                if(!ec)
                {
                     //std::cout << "Success write to SOCKS4_server" << std::endl;
                    do_read_SOCKS4_reply();
                }
                else
                     perror("couldn't write to socks4_server");
            });

        }

        void do_read_SOCKS4_reply()
        {
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, 8),
            [this, self](boost::system::error_code ec, size_t length) {
                if (!ec) 
                {    
                  if (data_[1] == 90) 
                  {
                     //cout << "Sucess to receiving SOCKS4 reply" << endl;
                     do_read();
                  }
                }
        });
        }

        void do_connect(tcp::resolver::iterator it)
        {
            //cout<<"success to do_connect()"<<endl; //還沒進去 server
            auto self(shared_from_this());
            socket_.async_connect(*it, [this, self](boost::system::error_code ec)
            {
               //cout<<socket_.is_open()<<" "<<session_id<<endl;
                if (!ec)
                {
                  //cout<<socket_.is_open()<<" "<<session_id<<endl;
                  do_write_SOCKS4_request();
                }
                else
                    std::cout << "can't connect to socks4_server" << std::endl;
            });
        }

        void do_resolve() 
        {
            auto self(shared_from_this());
            resolv_.async_resolve(query_, [this, self](boost::system::error_code ec, tcp::resolver::iterator it)
            {
                if(!ec)
                {
                  //cout << "do_resolve() "<<session_id<<endl;
                  do_connect(it);
                }
                else
                  std::cerr << "can't resolve to socks4_server" << std::endl;
            });
        }
};


int main (int argc, char* argv[]) {

   try 
   {  
      Parse_QUERY_STRING();
      Show_HTML();
      boost::asio::io_context io_context;
      for (int i = 0; i < 5; i++)
      {
         if (host_name[i] == "")
            break;
         std::make_shared<Session>(io_context, host_name[i], port_number[i], file_name[i], i)->start();
      }

      io_context.run();
   } 
   catch (std::exception &e) 
   {
      cout << "exception: " << e.what() << "\n";
   }
   
   return 0;
}